//------------------------------------------------------------------------------
// blueprints2-example's documents, debugger and overlay, without its window.
//------------------------------------------------------------------------------
# include "harness.h"
# include "blueprint_editor_document.h"
# include "blueprint_editor_utilities.h"
# include "crude_blueprint.h"
# include "crude_blueprint_library.h"
# include "crude_logger.h"
# include "imgui_extras.h"
# include <crude_json.h>
# include <cstdio>
# include <functional>
# include <memory>
# include <string>
# include <utility>
# include <vector>

using namespace crude_blueprint;
using blueprint_editor::Document;
using blueprint_editor_utilities::CanvasActions;
using blueprint_editor_utilities::DebugOverlay;
using blueprint_editor_utilities::Debugger;

namespace {

// As the example has it.
std::shared_ptr<const Registry> ExampleRegistry()
{
    static const std::shared_ptr<const Registry> registry = []
    {
        auto made = std::make_shared<Registry>();
        if (!RegisterBuiltins(*made) || !RegisterLibrary(*made))
            test::Check(false, "the example's registry is made");
        return made;
    }();
    return registry;
}

struct Prints final : ContextMonitor
{
    void OnLog(const Context&, Id, std::string_view text) override
    {
        m_Text += (m_Text.empty() ? "" : " ") + std::string(text);
    }

    std::string m_Text;
};

ed::Config UnnamedConfig()
{
    ed::Config config;
    config.SettingsName = nullptr;
    return config;
}

// The example logs through the overlay logger it shows, which needs an ImGui context.
struct OverlayLog
{
    OverlayLog()  { crude_logger::OverlayLogger::SetCurrent(&m_Logger); }
    ~OverlayLog() { crude_logger::OverlayLogger::SetCurrent(nullptr); }

    crude_logger::OverlayLogger m_Logger;
};

struct TestFile
{
    explicit TestFile(const char* path): Path(path) { std::remove(Path.c_str()); }
    ~TestFile() { std::remove(Path.c_str()); }

    std::string Path;
};

// A document of the example's blueprint, laid out in the session's editor: Start runs For Loop
// from 0 to 4, whose body prints each index plus 10, then Print says "Done".
struct Example
{
    explicit Example(test::Session& session)
    {
        session.Use([&] { blueprint_editor::CreateExampleBlueprint(m_Document); });
    }

    // The nth node of a type, in the order the example makes them.
    Id Node(const char* type, int nth = 0) const
    {
        for (auto& node : m_Document.GetGraph().Nodes())
            if (node.m_Type->m_Name == type && nth-- == 0)
                return node.m_Id;
        test::Check(false, "the example has a %s", type);
        return 0;
    }

    Id Pin(Id node, std::string_view key) const
    {
        auto found = m_Document.GetGraph().FindNode(node);
        auto pin   = found ? found->FindPin(key) : nullptr;
        if (!pin)
            test::Check(false, "node %d has a pin %s", static_cast<int>(node), std::string(key).c_str());
        return pin ? pin->m_Id : 0;
    }

    Document m_Document{ ExampleRegistry(), std::make_shared<TypeTable>() };
};

std::string Message(const Result<void>& result)
{
    return result ? std::string() : std::string(result.GetError().Message());
}

} // namespace

TEST_CASE(ExampleRuns, "blueprints2.the_example_blueprint_runs", nullptr, test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    Example       example(session);
    auto compiled = Compile(example.m_Document.GetBlueprint());
    if (!compiled.m_Program)
        return test::Check(false, "the example compiles");

    Context context(compiled.m_Program);
    Prints  prints;
    context.SetMonitor(&prints);
    auto fiber = context.Start("Start");
    auto state = FiberState::Ready;
    for (int i = 0; i < 100 && state == FiberState::Ready; ++i)
        state = context.Step(fiber.ValueOr(0)).ValueOr(FiberState::Failed);
    test::Check(state == FiberState::Done && prints.m_Text == "10 11 12 13 14 Done", "it prints each index plus 10, then Done: %s", prints.m_Text.c_str());
}

TEST_CASE(EditsStopRuns, "blueprints2.an_edit_stops_a_run_and_a_move_doesnt", nullptr, test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto  loop     = example.Node("crude.ForLoop");

    Debugger debugger;
    auto started = debugger.Start(document, false);
    debugger.Step();
    debugger.Step();
    test::Check(started && debugger.IsActive() && debugger.CurrentNode() == loop && !debugger.IsRunning(), "a run starts paused, and steps");

    // An undo step of the layout, which leaves the blueprint as it was.
    session.Use([&]
    {
        auto transaction = document.BeginUndoTransaction("Move");
        ed::SetNodePosition(loop, ImVec2(300.0f, 300.0f));
        transaction->AddAction("Move For Loop");
    });
    debugger.Update(document);
    test::Check(debugger.IsActive() && document.m_Undo.size() == 1, "moving a node leaves the run");

    session.Use([&]
    {
        auto transaction = document.BeginUndoTransaction("Edit Value");
        if (document.GetGraph().SetValue(example.Pin(loop, "last"), Value::Int32(2)))
            transaction->AddAction("For Loop's last index changed");
    });
    debugger.Update(document);
    test::Check(!debugger.IsActive(), "changing a value stops it");

    debugger.Start(document, false);
    session.Use([&] { document.Undo(); });
    debugger.Update(document);
    test::Check(!debugger.IsActive(), "and so does undoing it");

    NodeConfig untyped;
    if (!document.GetGraph().AddNode("crude.Constant", untyped))
        test::Check(false, "a Constant is added");
    test::Check(!debugger.Start(document, false) && !debugger.IsActive(), "a blueprint with an error doesn't start: a Constant of no type");
}

TEST_CASE(DebugOverlayLayout, "blueprints2.debug_overlay_leaves_the_layout_alone", nullptr, test::TestFlags_None)
{
    // While a run is paused, the overlay draws values beside pins: after an output pin, before an
    // input pin. Each pin's cell in the node's layout is a group that ends after the overlay, and
    // has to end with the pin.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& graph = example.m_Document.GetGraph();
    auto  loop  = example.Node("crude.ForLoop"), done = example.Node("crude.Print", 1);

    // A value much wider than its pin, linked into Done's Print.
    NodeConfig wide;
    wide.m_Types["T"]     = &StringType();
    wide.m_Data["value"] = Value::String("a value much wider than its pin");
    auto constant = graph.AddNode("crude.Constant", wide);
    if (!constant || !graph.Connect(example.Pin(constant.GetValue(), "value"), example.Pin(done, "text")))
        return test::Check(false, "the wide value is linked");

    Debugger debugger;
    debugger.Start(example.m_Document, false);
    debugger.Step();
    debugger.Step();

    struct Cell
    {
        float PinEnd  = 0.0f;
        float CellEnd = 0.0f;
    };
    Cell index, body, text;

    session.Frame([&](int)
    {
        DebugOverlay overlay(debugger);
        auto cell = [&overlay, &graph](Id id, ed::PinKind kind, Cell& result)
        {
            auto& pin = *graph.FindPin(id);
            ImGui::BeginGroup();
            ed::BeginPin(id, kind);
            ImGui::TextUnformatted("Pin");
            ed::EndPin();
            result.PinEnd = ImGui::GetItemRectMax().x;
            if (kind == ed::PinKind::Output)
                overlay.DrawOutputPin(pin);
            else
                overlay.DrawInputPin(pin, true);
            ImGui::EndGroup();
            result.CellEnd = ImGui::GetItemRectMax().x;
        };

        overlay.Begin();
        ed::BeginNode(loop);
        cell(example.Pin(loop, "index"), ed::PinKind::Output, index);
        cell(example.Pin(loop, "body"), ed::PinKind::Output, body);
        ed::EndNode();
        ed::BeginNode(done);
        cell(example.Pin(done, "text"), ed::PinKind::Input, text);
        ed::EndNode();
        overlay.End();
    });

    test::Check(test::AssertCount() == 0, "the groups end without an IM_ASSERT: %d", test::AssertCount());
    test::Check(index.CellEnd == index.PinEnd, "For Loop's Index ends its cell, with 0 after it: at %g, the pin at %g", index.CellEnd, index.PinEnd);
    test::Check(body.CellEnd == body.PinEnd, "so does its Loop Body, with no value: at %g, the pin at %g", body.CellEnd, body.PinEnd);
    test::Check(text.CellEnd == text.PinEnd, "and Done's Text, with a value wider than it before it: at %g, the pin at %g", text.CellEnd, text.PinEnd);
}

TEST_CASE(DebugOverlayFlow, "blueprints2.debug_overlay_flows_only_the_link_the_step_took", nullptr, test::TestFlags_None)
{
    // A run paused after For Loop, which took its Loop Body to Print. Every node shows its pins in
    // two rows, with the overlay's values beside them.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& graph = example.m_Document.GetGraph();
    auto  loop  = example.Node("crude.ForLoop"), print = example.Node("crude.Print");

    Debugger debugger;
    debugger.Start(example.m_Document, false);
    debugger.Step();
    debugger.Step();

    const ImU32 flowColor = IM_COL32(255, 0, 255, 255);
    ImVec2 body, enter;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
        {
            auto& style = ed::GetStyle();
            style.Colors[ed::StyleColor_Flow]       = ImColor(flowColor);
            style.Colors[ed::StyleColor_FlowMarker] = ImColor(flowColor);
        }

        DebugOverlay overlay(debugger);
        overlay.Begin();
        for (auto& node : graph.Nodes())
        {
            ed::BeginNode(node.m_Id);
            ImGui::TextUnformatted(node.m_Type->m_Title.c_str());
            for (auto direction : { Direction::Input, Direction::Output })
            {
                if (direction == Direction::Output)
                    ImGui::SameLine(0.0f, 40.0f);
                ImGui::BeginGroup();
                for (auto& pin : node.m_Pins)
                {
                    if (pin.m_Direction != direction)
                        continue;
                    ed::BeginPin(pin.m_Id, direction == Direction::Input ? ed::PinKind::Input : ed::PinKind::Output);
                    ImGui::TextUnformatted(direction == Direction::Input ? "> in" : "out >");
                    ed::EndPin();
                    const auto center = ed::CanvasToScreen((ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f);
                    if (pin.m_Id == example.Pin(loop, "body"))
                        body = center;
                    if (pin.m_Id == example.Pin(print, "enter"))
                        enter = center;
                    if (direction == Direction::Input)
                        overlay.DrawInputPin(pin, true);
                    else
                        overlay.DrawOutputPin(pin);
                }
                ImGui::EndGroup();
            }
            ed::EndNode();
        }
        for (auto& link : graph.Links())
            ed::Link(link.m_Id, link.m_From, link.m_To);
        overlay.End();
    };

    int inside = 0, outside = 0;
    session.AfterEditor = [&]
    {
        // Flow is drawn over the whole link: its vertices may stray from the line between the
        // pins by the link's thickness.
        const ImRect taken(ImMin(body, enter) - ImVec2(8.0f, 8.0f), ImMax(body, enter) + ImVec2(8.0f, 8.0f));
        const auto geometry = test::Capture(session.DrawList);
        inside = outside = 0;
        for (size_t i = 0; i < geometry.Colors.size(); ++i)
            if ((geometry.Colors[i] & ~IM_COL32_A_MASK) == (flowColor & ~IM_COL32_A_MASK))
                ++(taken.Contains(geometry.Positions[i]) ? inside : outside);
    };
    session.Run(3);

    test::Check(debugger.CurrentNode() == loop && debugger.NextNode() == print, "the run paused after For Loop, with Print next");
    test::Check(inside > 0, "the link from Loop Body to Print flows: %d flow vertices", inside);
    test::Check(outside == 0, "and no other link does: %d flow vertices elsewhere", outside);

    // The next step runs Print, and its branch ends, through no link. Frames take 1/60 s.
    debugger.Step();
    session.Run(15);
    const int fading = inside + outside;
    session.Run(21);
    const int faded = inside + outside;
    test::Check(fading > 0, "a quarter of a second after the step, the link it took still fades: %d flow vertices", fading);
    test::Check(faded == 0, "and 0.6 seconds after it, no link flows: %d flow vertices", faded);
}

TEST_CASE(SaveLiveState, "blueprints2.save_writes_the_blueprint_as_it_is", "EXB-5", test::TestFlags_None)
{
    // For Loop's last index changes, as an edit in place did, after the last undo step.
    test::Session session(UnnamedConfig());
    Example       example(session);
    auto loop = example.Node("crude.ForLoop");
    auto last = example.Pin(loop, "last");
    auto set  = example.m_Document.GetGraph().SetValue(last, Value::Int32(7));

    TestFile file("blueprints2_save.bp");
    const bool saved = session.Use([&] { return example.m_Document.Save(file.Path); });
    Document reloaded(ExampleRegistry(), std::make_shared<TypeTable>());
    const bool loaded = reloaded.Load(file.Path);
    auto value = loaded ? reloaded.GetGraph().FindValue(last) : nullptr;

    test::Check(set && saved && loaded, "the document saves and loads");
    test::Check(value && *value == Value::Int32(7), "with the last index For Loop had when saved");
}

TEST_CASE(StepsMarkModified, "blueprints2.undo_steps_mark_the_document_modified", "EXB-5", test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto  last     = example.Pin(example.Node("crude.ForLoop"), "last");
    bool fresh = true, edited = false, undone = false, redone = false;
    Value afterUndo, afterRedo;
    session.Use([&]
    {
        fresh = document.m_IsModified;
        {
            auto transaction = document.BeginUndoTransaction("Edit Value");
            if (document.GetGraph().SetValue(last, Value::Int32(7)))
                transaction->AddAction("last index changed");
        }
        edited = document.m_IsModified;

        // As saving does.
        document.m_IsModified = false;
        document.Undo();
        undone = document.m_IsModified;
        if (auto value = document.GetGraph().FindValue(last))
            afterUndo = *value;

        document.m_IsModified = false;
        document.Redo();
        redone = document.m_IsModified;
        if (auto value = document.GetGraph().FindValue(last))
            afterRedo = *value;
    });

    test::Check(!fresh, "a new document has no changes");
    test::Check(edited, "an undo step marks it modified");
    test::Check(undone && redone, "and so do undo and redo: %d, %d", undone, redone);
    test::Check(afterUndo == Value::Int32(4) && afterRedo == Value::Int32(7), "undo brings back the value before the edit, and redo the edit");
}

TEST_CASE(MalformedDocuments, "blueprints2.malformed_documents_are_refused", "EXB-8", test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    const auto saved = session.Use([&] { return example.m_Document.Serialize(); });

    using Change = std::function<void(crude_json::value&)>;
    const std::pair<const char*, Change> changes[] =
    {
        { "an array",                       [](crude_json::value& v) { v = crude_json::array{}; } },
        { "no document",                    [](crude_json::value& v) { v.erase("document"); } },
        { "a node state of key x",          [](crude_json::value& v) { v["document"]["nodes"]["x"] = crude_json::object{}; } },
        { "a selection of no ID",           [](crude_json::value& v) { v["document"]["selection"]["nodes"].push_back(-1); } },
        { "a blueprint of an older format", [](crude_json::value& v) { v["document"]["blueprint"]["version"] = 1; } },
        { "a blueprint of no graph",        [](crude_json::value& v) { v["document"]["blueprint"]["graphs"] = crude_json::array{}; } },
        { "a blueprint of a duplicate ID",  [](crude_json::value& v) { v["document"]["blueprint"]["graphs"][0]["nodes"][1]["id"] = v["document"]["blueprint"]["graphs"][0]["nodes"][0]["id"]; } },
    };

    Document control(ExampleRegistry(), std::make_shared<TypeTable>());
    test::Check(control.Deserialize(saved) && control.GetGraph().Nodes().size() == example.m_Document.GetGraph().Nodes().size(), "the saved document loads");

    for (auto& change : changes)
    {
        auto changed = saved;
        change.second(changed);

        Document target(ExampleRegistry(), std::make_shared<TypeTable>());
        auto kept = target.GetGraph().AddNode("crude.Start");
        const bool loaded = target.Deserialize(changed);
        test::Check(!loaded && kept && target.GetGraph().Nodes().size() == 1 && target.GetGraph().FindNode(kept.GetValue()), "%s is refused, and the document kept", change.first);
    }
}

TEST_CASE(Utf8Documents, "blueprints2.utf8_text_and_paths_round_trip", "EXB-9", test::TestFlags_None)
{
    // Polish text in a Print's text and in the file name, in UTF-8, as the file dialogs give paths.
    const char* text = "za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87";
    const char* path = "blueprints2_za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87.bp";
    test::Session session(UnnamedConfig());
    Example       example(session);
    auto done = example.Pin(example.Node("crude.Print", 1), "text");
    auto set  = example.m_Document.GetGraph().SetValue(done, Value::String(text));

    const bool saved = session.Use([&] { return example.m_Document.Save(path); });
    Document reloaded(ExampleRegistry(), std::make_shared<TypeTable>());
    const bool loaded = reloaded.Load(path);
    auto value = loaded ? reloaded.GetGraph().FindValue(done) : nullptr;

    test::Check(set && saved && loaded, "a document saves under a UTF-8 path and loads back: %s", Message(set).c_str());
    test::Check(value && *value == Value::String(text), "with the UTF-8 text of its pin");

# ifdef _WIN32
    test::Check(_wremove(L"blueprints2_za\u017C\u00F3\u0142\u0107.bp") == 0, "the file has the name the path spells");
# else
    std::remove(path);
# endif
}

TEST_CASE(RecentFilesIni, "blueprints2.recent_files_take_only_indices_in_order", "EXB-11", test::TestFlags_None)
{
    // An imgui.ini edited by hand. The list writes its entries from index 0 up.
    test::Host host;
    ImEx::MostRecentlyUsedList::Install(host.Context);
    ImGui::LoadIniSettingsFromMemory(
        "[MostRecentlyUsedList][EXB-11]\n"
        "0=a.bp\n"
        "-1=negative.bp\n"
        "1=b.bp\n"
        "7=gap.bp\n"
        "4294967296=wraps.bp\n"
        "2000000000=huge.bp\n"
        "1=b2.bp\n");

    ImEx::MostRecentlyUsedList list("EXB-11");
    std::string entries;
    for (auto& entry : list.GetList())
        entries += entry + "|";

    test::Check(entries == "a.bp|b2.bp|", "only entries at an index of the list, or the next one, are read: %s", entries.c_str());
}

TEST_CASE(LogSymbolAtEnd, "blueprints2.the_log_shows_messages_that_end_with_a_symbol", nullptr, test::TestFlags_None)
{
    // The log colors a message by ranges of it; a debug build checks the iterators they're made with.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    LOGI("Execution: Done (13 steps)");
    LOGI("[Print] Print#11: \"10\"");
    session.AfterEditor = [&] { log.m_Logger.Draw(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()); };
    session.Frame();

    const ImU32 stringColor = IM_COL32(255, 174, 133, 255);
    test::Check(test::AssertCount() == 0, "both messages show: %d IM_ASSERTs", test::AssertCount());
    test::Check(test::FirstIndexOf(test::Capture(session.DrawList), stringColor) >= 0, "with the quotes around \"10\" in the color of strings");
}

TEST_CASE(CanvasActionsFire, "blueprints2.only_the_first_active_canvas_action_fires", nullptr, test::TestFlags_None)
{
    // Actions on F5, declared after the editor as the example does: one inactive, two of the
    // canvas, and a global one.
    test::Session session(UnnamedConfig());
    bool overCanvas = true;
    int  fired[4]   = {};
    session.AfterEditor = [&]
    {
        CanvasActions actions(overCanvas);
        fired[0] += actions.Press("Inactive", ImGuiKey_F5, false);
        fired[1] += actions.Press("First", ImGuiKey_F5, true);
        fired[2] += actions.Press("Second", ImGuiKey_F5, true);
        fired[3] += actions.Press("Global", ImGuiKey_F5, true, true);
    };
    auto press = [&]
    {
        test::Key(ImGuiKey_F5, true);
        session.Run(1);
        test::Key(ImGuiKey_F5, false);
        session.Run(2);
    };

    // A Shortcut() route takes effect the frame after it's declared.
    session.Run(2);
    press();
    test::Check(fired[0] == 0 && fired[1] == 1 && fired[2] == 0 && fired[3] == 0, "over the canvas, a press fires the first active action alone: %d %d %d %d", fired[0], fired[1], fired[2], fired[3]);

    overCanvas = false;
    press();
    test::Check(fired[1] == 1 && fired[2] == 0 && fired[3] == 1, "elsewhere, only the global one: %d %d %d", fired[1], fired[2], fired[3]);
}

TEST_CASE(GroupPinEdits, "blueprints2.group_pins_are_added_and_removed_in_undo_steps", nullptr, test::TestFlags_None)
{
    // The example's Add has in.0, from For Loop's index, and in.1, from the Constant.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto  add      = example.Node("crude.Add");
    auto  inputs   = [&] { return document.GetGraph().FindNode(add)->m_Pins.size() - 1; };
    auto  linkedTo = [&](std::string_view key)
    {
        auto pin = document.GetGraph().FindNode(add)->FindPin(key);
        return pin && blueprint_editor_utilities::HasLinks(document.GetGraph(), pin->m_Id);
    };

    size_t grown = 0, shrunk = 0;
    bool   kept = false, gone = false;
    session.Use([&]
    {
        blueprint_editor_utilities::AddGroupPin(document, add, "in");
        grown = inputs();

        // The name in the node's declaration, which the edit replaces, as the pin's menu passes it.
        auto& group = document.GetGraph().FindNode(add)->m_Groups.front();
        blueprint_editor_utilities::RemoveGroupPin(document, add, group.m_Name, 1);
        shrunk = inputs();
        kept   = linkedTo("in.0");
        gone   = !linkedTo("in.1");
    });

    test::Check(grown == 3 && shrunk == 2, "Add grows to 3 inputs, and back to 2: %d, %d", static_cast<int>(grown), static_cast<int>(shrunk));
    test::Check(kept && gone, "in.1 went with its link, and in.2, unlinked, took its place");
    test::Check(document.m_Undo.size() == 2 && document.m_Undo[0].m_Name.find("added") != std::string::npos && document.m_Undo[1].m_Name.find("removed") != std::string::npos,
        "each is an undo step: %s; %s", document.m_Undo.empty() ? "-" : document.m_Undo[0].m_Name.c_str(), document.m_Undo.size() < 2 ? "-" : document.m_Undo[1].m_Name.c_str());

    session.Use([&] { document.Undo(); });
    test::Check(inputs() == 3 && linkedTo("in.1"), "undo brings in.1 back with its link");
}

TEST_CASE(NewDocuments, "blueprints2.a_new_document_has_an_empty_graph", nullptr, test::TestFlags_None)
{
    Document document(ExampleRegistry(), std::make_shared<TypeTable>());
    test::Check(document.GetBlueprint().Graphs().size() == 1 && document.GetGraph().Nodes().empty() && document.GetGraph().GetName() == "Event Graph", "one graph, of no nodes");
    test::Check(!document.m_IsModified && document.m_Undo.empty() && document.m_Redo.empty(), "and no changes to undo");
}
