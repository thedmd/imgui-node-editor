//------------------------------------------------------------------------------
// blueprints2-example's documents, debugger and overlay, without its window.
//------------------------------------------------------------------------------
# include "harness.h"
# include "blueprint_editor_document.h"
# include "blueprint_editor_utilities.h"
# include "crude_blueprint.h"
# include "crude_logger.h"
# include "imgui_extras.h"
# include <crude_json.h>
# include <algorithm>
# include <cstdio>
# include <functional>
# include <memory>
# include <string>
# include <utility>
# include <vector>

using namespace crude_blueprint;
using blueprint_editor::Document;
using blueprint_editor::ExampleRegistry;
using blueprint_editor_utilities::CanvasActions;
using blueprint_editor_utilities::Debugger;

namespace {

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

TEST_CASE(EditsDuringRuns, "blueprints2.a_run_goes_on_after_an_edit_of_values_and_stops_after_others", nullptr, test::TestFlags_None)
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

    auto last = [&] { auto read = debugger.GetContext() ? debugger.GetContext()->Evaluate(example.Pin(loop, "last")) : Result<Watch>(BlueprintError::NotFound); return read ? read.GetValue().m_Value : Value(); };
    session.Use([&]
    {
        auto transaction = document.BeginUndoTransaction("Edit Value");
        if (document.GetGraph().SetValue(example.Pin(loop, "last"), Value::Int32(2)))
            transaction->AddAction("For Loop's last index changed");
    });
    debugger.Update(document);
    test::Check(debugger.IsActive() && last() == Value::Int32(2), "changing a value goes on in the run, which reads the new one");

    session.Use([&] { document.Undo(); });
    debugger.Update(document);
    test::Check(debugger.IsActive() && last() == Value::Int32(4), "and so does undoing it");

    session.Use([&]
    {
        auto transaction = document.BeginUndoTransaction("Break Link");
        if (document.GetGraph().Disconnect(document.GetGraph().Links().front().m_Id))
            transaction->AddAction("A link broken");
    });
    debugger.Update(document);
    test::Check(!debugger.IsActive(), "and breaking a link stops it");

    NodeConfig untyped;
    if (!document.GetGraph().AddNode("crude.Constant", untyped))
        test::Check(false, "a Constant is added");
    test::Check(!debugger.Start(document, false) && !debugger.IsActive(), "a blueprint with an error doesn't start: a Constant of no type");
}

TEST_CASE(Breakpoints, "blueprints2.a_run_pauses_at_breakpoints_which_stay_for_later_runs", nullptr, test::TestFlags_None)
{
    // A breakpoint on the Print in For Loop's body.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto  print    = example.Node("crude.Print");

    Debugger debugger;
    auto finish = [&] { for (int i = 0; i < 100 && debugger.IsRunning(); ++i) debugger.Update(document); };
    debugger.SetBreakpoint(print, true);
    auto started = debugger.Start(document, true);
    debugger.Update(document);
    test::Check(started && debugger.IsActive() && !debugger.IsRunning() && debugger.IsAtBreakpoint() && debugger.NextNode() == print, "a run pauses before the Print");

    const auto steps = debugger.GetContext()->StepCount();
    debugger.Continue();
    debugger.Update(document);
    test::Check(debugger.IsAtBreakpoint() && debugger.GetContext()->StepCount() == steps + 2, "continued, it runs the Print and pauses there again, once round the loop: %d steps",
        static_cast<int>(debugger.GetContext()->StepCount() - steps));

    debugger.SetBreakpoint(print, false);
    debugger.Continue();
    finish();
    test::Check(!debugger.IsActive() && !debugger.HasBreakpoint(print), "without the breakpoint, it runs to its end");

    debugger.SetBreakpoint(print, true);
    debugger.Start(document, true);
    debugger.Update(document);
    test::Check(debugger.IsAtBreakpoint() && debugger.NextNode() == print, "a breakpoint stays for the runs that follow");
    debugger.ClearBreakpoints();
    debugger.Continue();
    finish();
    test::Check(!debugger.IsActive() && !debugger.HasBreakpoint(print), "until it's cleared");
}

TEST_CASE(LiveValues, "blueprints2.values_show_from_the_run_else_as_an_unstarted_run_computes_them", nullptr, test::TestFlags_None)
{
    // The example's Add sums For Loop's Index and the Constant's 10.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto  add      = example.Node("crude.Add"), loop = example.Node("crude.ForLoop"), ten = example.Node("crude.Constant");
    using blueprint_editor_utilities::WhenWritten;

    Debugger debugger;
    auto read = [&](Id node, const char* key) { auto watch = debugger.Evaluate(document, example.Pin(node, key)); return watch ? watch.GetValue() : Watch(); };
    auto sum  = read(add, "result");
    auto index = read(loop, "index");
    test::Check(sum.m_Value == Value::Int32(10) && WhenWritten(sum, debugger.StepCount()).empty(), "with no run, Add's result is what it computes, 10 from 0 and 10, before any step");
    test::Check(index.m_Value == Value::Int32(0) && WhenWritten(index, debugger.StepCount()) == "not run yet", "and For Loop's Index, which no step wrote, reads its default");

    session.Use([&]
    {
        NodeConfig twenty = document.GetGraph().FindNode(ten)->m_Config;
        twenty.m_Data["value"] = Value::Int32(20);
        auto transaction = document.BeginUndoTransaction("Edit Value");
        if (document.GetGraph().SetConfig(ten, twenty))
            transaction->AddAction("Constant changed");
    });
    test::Check(read(add, "result").m_Value == Value::Int32(20), "a change to the document computes it again");

    debugger.Start(document, false);
    for (int i = 0; i < 4; ++i)
        debugger.Step();
    auto stepped = read(loop, "index");
    test::Check(stepped.m_Value == Value::Int32(1) && WhenWritten(stepped, debugger.StepCount()) == "this step", "during a run, the run's values: For Loop wrote 1 this step");
    debugger.Step();
    test::Check(WhenWritten(read(loop, "index"), debugger.StepCount()) == "step 4", "and a step later, it was step 4's");
}

TEST_CASE(WatchedPins, "blueprints2.watched_pins_keep_the_order_they_were_added_in", nullptr, test::TestFlags_None)
{
    Debugger debugger;
    debugger.SetWatched(5, true);
    debugger.SetWatched(3, true);
    debugger.SetWatched(5, true);
    auto watched = std::vector<Id>(debugger.Watched().begin(), debugger.Watched().end());
    test::Check(watched == std::vector<Id>{ 5, 3 } && debugger.IsWatched(3), "pins are watched once each, in the order they were added");
    debugger.SetWatched(5, false);
    debugger.SetWatched(7, false);
    watched.assign(debugger.Watched().begin(), debugger.Watched().end());
    test::Check(watched == std::vector<Id>{ 3 } && !debugger.IsWatched(5), "and unwatched, others keep their places");
}

TEST_CASE(TakenLinkFlows, "blueprints2.only_the_link_the_last_step_took_flows", nullptr, test::TestFlags_None)
{
    // A run paused after For Loop, which took its Loop Body to Print. Every node shows its pins in
    // two rows.
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
                }
                ImGui::EndGroup();
            }
            ed::EndNode();
        }
        for (auto& link : graph.Links())
            ed::Link(link.m_Id, link.m_From, link.m_To);
        blueprint_editor_utilities::FlowTakenLink(debugger);
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

    // A chord of two modifiers fires with both held, and not with one.
    int chord = 0;
    session.AfterEditor = [&]
    {
        CanvasActions actions(true);
        chord += actions.Press("Chord", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_D, true);
    };
    session.Run(2);
    auto pressD = [&](bool shift)
    {
        test::Key(ImGuiMod_Ctrl, true);
        test::Key(ImGuiMod_Shift, shift);
        session.Run(1);
        test::Key(ImGuiKey_D, true);
        session.Run(1);
        test::Key(ImGuiKey_D, false);
        test::Key(ImGuiMod_Shift, false);
        test::Key(ImGuiMod_Ctrl, false);
        session.Run(2);
    };
    pressD(false);
    pressD(true);
    test::Check(chord == 1, "Ctrl+Shift+D fires once, for Ctrl+Shift and D, not for Ctrl and D: %d", chord);
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

TEST_CASE(KeptValues, "blueprints2.an_undecided_pin_shows_the_value_it_keeps", nullptr, test::TestFlags_None)
{
    // A new Add, whose T the example's Constant decides as Int32 through in.0.
    test::Session session(UnnamedConfig());
    Example       example(session);
    auto& graph = example.m_Document.GetGraph();
    auto  ten   = example.Node("crude.Constant");
    auto  add   = graph.AddNode("crude.Add").ValueOr(0);
    auto  link  = graph.Connect(example.Pin(ten, "value"), example.Pin(add, "in.0"));
    auto  set   = graph.SetValue(example.Pin(add, "in.1"), Value::Int32(5));
    auto  in1   = [&] { return *graph.FindPin(example.Pin(add, "in.1")); };
    using blueprint_editor_utilities::KeptValue;
    using blueprint_editor_utilities::InputValue;
    test::Check(link && set && InputValue(graph, in1()) == Value::Int32(5) && !KeptValue(graph, in1()).IsValid(), "decided, in.1 reads the 5 typed into it, and keeps nothing aside");

    auto unlinked = graph.Disconnect(link.ValueOr(0));
    test::Check(unlinked && KeptValue(graph, in1()) == Value::Int32(5) && !InputValue(graph, in1()).IsValid() && !blueprint_editor_utilities::HasValueEditor(graph, in1()),
        "undecided once unlinked, it keeps the 5, with no editor");

    // As one whose chosen type was reset.
    NodeConfig untyped;
    untyped.m_Data["value"] = Value::Int32(7);
    auto constant = graph.AddNode("crude.Constant", untyped);
    auto value    = constant ? graph.FindNode(constant.GetValue())->FindPin("value") : nullptr;
    test::Check(value && KeptValue(graph, *value) == Value::Int32(7), "an undecided Constant keeps its value too");
}

TEST_CASE(LinkEdits, "blueprints2.links_are_made_in_undo_steps", nullptr, test::TestFlags_None)
{
    // The example's Constant, of Int32, into the String that Done's Print takes.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto& graph    = document.GetGraph();
    auto  from     = example.Pin(example.Node("crude.Constant"), "value");
    auto  to       = example.Pin(example.Node("crude.Print", 1), "text");
    auto  cast     = [&]() -> const Link*
    {
        for (auto& link : graph.Links())
            if (link.m_From == from && link.m_To == to)
                return &link;
        return nullptr;
    };

    session.Use([&] { blueprint_editor_utilities::CreateLink(document, from, to); });
    test::Check(!cast() && document.m_Undo.empty(), "as it is, it doesn't fit, and makes no undo step");

    session.Use([&] { blueprint_editor_utilities::CreateLink(document, from, to, LinkMode::Cast); });
    test::Check(cast() && cast()->m_Cast == "Int32->String" && document.m_Undo.size() == 1 && document.m_Undo[0].m_Name.ends_with(", cast"),
        "through a cast, it's made in one step: %s", document.m_Undo.empty() ? "-" : document.m_Undo[0].m_Name.c_str());
}

TEST_CASE(ProblemLists, "blueprints2.problems_are_listed_by_their_nodes", nullptr, test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto& graph    = document.GetGraph();
    auto  edit     = [&](const std::function<void()>& change)
    {
        session.Use([&]
        {
            auto transaction = document.BeginUndoTransaction("Edit");
            change();
            transaction->AddAction("edited");
        });
    };
    Debugger                                debugger;
    blueprint_editor_utilities::ProblemList problems;
    problems.Update(document, debugger);
    test::Check(problems.Problems().empty() && !problems.BlocksRun(), "the example has none");

    // Compiling refuses a new Add of no type: an error for each of its pins.
    Id add = 0;
    edit([&] { add = graph.AddNode("crude.Add").ValueOr(0); });
    problems.Update(document, debugger);
    test::Check(problems.Count(Severity::Error, add) == 3 && problems.Count(Severity::Error) == 3 && problems.BlocksRun(), "a new Add has 3 errors, which block a run: %d", problems.Count(Severity::Error));

    // A Float forced into the example's Add disables both links of its Int32, and each problem is
    // the link's input's node's.
    NodeConfig floats;
    floats.m_Types["T"] = &FloatType();
    auto sum = example.Node("crude.Add"), print = example.Node("crude.Print");
    edit([&]
    {
        auto constant = graph.AddNode("crude.Constant", floats).ValueOr(0);
        if (!graph.Connect(example.Pin(constant, "value"), example.Pin(sum, "in.1"), LinkMode::Force))
            test::Check(false, "the Float is forced");
    });
    problems.Update(document, debugger);
    test::Check(problems.Count(Severity::Error, sum) == 1 && problems.Count(Severity::Error, print) == 1, "For Loop's index into Add, and Add's result into Print: %d, %d",
        problems.Count(Severity::Error, sum), problems.Count(Severity::Error, print));
    for (auto& problem : problems.Problems())
        if (problem.m_Node == print)
            test::Check(blueprint_editor_utilities::ProblemList::Text(graph, problem).starts_with("text: "), "and names the input: %s", blueprint_editor_utilities::ProblemList::Text(graph, problem).c_str());
}

TEST_CASE(RunProblems, "blueprints2.a_runs_error_is_listed_until_the_run_goes", nullptr, test::TestFlags_None)
{
    // The Constant as Int32's largest: Add overflows at index 1.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto& graph    = document.GetGraph();
    auto  ten      = example.Node("crude.Constant"), add = example.Node("crude.Add");
    auto  config   = graph.FindNode(ten)->m_Config;
    config.m_Data["value"] = Value::Int32(2147483647);
    auto set = graph.SetConfig(ten, config);

    Debugger debugger;
    debugger.Start(document, true);
    for (int i = 0; i < 100 && debugger.IsActive(); ++i)
        debugger.Step();
    blueprint_editor_utilities::ProblemList problems;
    problems.Update(document, debugger);
    auto runs = std::count_if(problems.Problems().begin(), problems.Problems().end(), [](auto& problem) { return problem.m_Run; });
    test::Check(set && runs == 1 && problems.Count(Severity::Error, add) == 1 && !problems.BlocksRun(), "the overflow is Add's error, which doesn't block running again: %d", static_cast<int>(runs));

    debugger.Stop();
    problems.Update(document, debugger);
    test::Check(problems.Problems().empty(), "and goes with the run");
}

TEST_CASE(SplitEdits, "blueprints2.struct_pins_split_and_recombine_in_undo_steps", nullptr, test::TestFlags_None)
{
    // A Make Vector2, whose output splits into x and y.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto& graph    = document.GetGraph();
    auto  make     = graph.AddNode("make:example.Vector2").ValueOr(0);
    auto  keys     = [&] { std::string text; for (auto& pin : graph.FindNode(make)->m_Pins) text += (text.empty() ? "" : " ") + pin.m_Key; return text; };

    std::string split, of, recombined;
    session.Use([&]
    {
        blueprint_editor_utilities::SplitStructPin(document, make, "struct", true);
        split = keys();
        of    = blueprint_editor_utilities::SplitOf(*graph.FindNode(make), "struct.y");
        blueprint_editor_utilities::SplitStructPin(document, make, "struct", false);
        recombined = keys();
    });
    test::Check(split == "x y struct.x struct.y" && of == "struct", "split, the output is its fields, of the split struct: %s; %s", split.c_str(), of.c_str());
    test::Check(recombined == "x y struct" && document.m_Undo.size() == 2, "recombined, it's whole again, in two undo steps: %s", recombined.c_str());
}

TEST_CASE(CreateNodeDialogs, "blueprints2.the_create_node_dialog_makes_what_its_search_finds_first", nullptr, test::TestFlags_None)
{
    // Opened from the example's Add.result, an Int32, "pr" finds only Print, whose text takes it
    // through a cast. Opened from no pin, "or" finds Or before For Loop, which has it inside.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto& graph    = document.GetGraph();
    auto  result   = example.Pin(example.Node("crude.Add"), "result");

    blueprint_editor_utilities::CreateNodeDialog dialog;
    std::function<void()> open;
    bool shown = false;
    session.DefaultContent = [&](int)
    {
        ed::Suspend();
        if (open)
            std::exchange(open, nullptr)();
        dialog.Show(document);
        shown = ImGui::IsPopupOpen("##create_node");
        ed::Resume();
    };
    auto type = [&](const char* text, ImGuiKey key)
    {
        session.Run(2);
        for (auto c = text; *c; ++c)
            ImGui::GetIO().AddInputCharacter(*c);
        session.Run(2);
        test::Key(key, true);
        session.Run(1);
        test::Key(key, false);
        session.Run(2);
    };
    auto count = [&] { return graph.Nodes().size(); };

    test::MouseMove(ImVec2(500.0f, 400.0f));
    const auto nodes = count();
    open = [&] { dialog.Open(result); };
    type("pr", ImGuiKey_Enter);
    auto print = graph.FindNode(dialog.GetCreatedNode());
    auto link  = std::find_if(graph.Links().begin(), graph.Links().end(), [&](const Link& other) { return other.m_From == result && print && other.m_To == print->FindPin("text")->m_Id; });
    test::Check(count() == nodes + 1 && print && print->m_Type->m_Name == "crude.Print" && !shown, "Enter makes a Print, and closes the dialog");
    test::Check(link != graph.Links().end() && link->m_Active && link->m_Cast == "Int32->String", "linked from Add.result through the cast");
    test::Check(document.m_Undo.size() == 1, "in one undo step: %d", static_cast<int>(document.m_Undo.size()));

    // Print begins with "p", but needs a cast; Append has it inside, and takes Int32 as it is.
    open = [&] { dialog.Open(result); };
    type("p", ImGuiKey_Enter);
    auto append = graph.FindNode(dialog.GetCreatedNode());
    test::Check(append && append->m_Type->m_Name == "crude.Append", "what needs no cast comes first: %s", append ? append->m_Type->m_Name.c_str() : "none");

    open = [&] { dialog.Open(); };
    type("or", ImGuiKey_Enter);
    auto made = graph.FindNode(dialog.GetCreatedNode());
    test::Check(count() == nodes + 3 && made && made->m_Type->m_Name == "crude.Or", "a title that begins with the search comes first: %s", made ? made->m_Type->m_Name.c_str() : "none");

    open = [&] { dialog.Open(); };
    type("or", ImGuiKey_Escape);
    test::Check(count() == nodes + 3 && !shown, "Esc closes the dialog, making nothing");
}

TEST_CASE(NewDocuments, "blueprints2.a_new_document_has_an_empty_graph", nullptr, test::TestFlags_None)
{
    Document document(ExampleRegistry(), std::make_shared<TypeTable>());
    test::Check(document.GetBlueprint().Graphs().size() == 1 && document.GetGraph().Nodes().empty() && document.GetGraph().GetName() == "Event Graph", "one graph, of no nodes");
    test::Check(!document.m_IsModified && document.m_Undo.empty() && document.m_Redo.empty(), "and no changes to undo");
}
