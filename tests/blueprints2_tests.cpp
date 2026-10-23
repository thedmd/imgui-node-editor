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
using blueprint_editor_utilities::RunArguments;
using blueprint_editor_utilities::ValueText;

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

    // The link from one pin into another; nullptr for none.
    const Link* LinkBetween(Id from, Id to) const
    {
        for (auto& link : m_Document.GetGraph().Links())
            if (link.m_From == from && link.m_To == to)
                return &link;
        return nullptr;
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
    auto evaluate = [&](Id node, const char* key) { return debugger.Evaluate(document, example.Pin(node, key)); };
    auto read     = [&](Id node, const char* key) { auto watch = evaluate(node, key); return watch ? watch.GetValue() : Watch(); };
    auto value = read(ten, "value");
    auto index = evaluate(loop, "index"), sum = evaluate(add, "result");
    test::Check(value.m_Value == Value::Int32(10) && WhenWritten(value, debugger.StepCount()).empty(), "with no run, the Constant's value is what it computes, before any step");
    test::Check(!index && index.GetError().Is(BlueprintError::NotRun) && !sum && sum.GetError().Is(BlueprintError::NotRun),
        "and For Loop's Index, which no step wrote, is undefined, as is Add's result, computed from it: %s", index ? "" : std::string(index.GetError().Message()).c_str());

    session.Use([&]
    {
        NodeConfig twenty = document.GetGraph().FindNode(ten)->m_Config;
        twenty.m_Data["value"] = Value::Int32(20);
        auto transaction = document.BeginUndoTransaction("Edit Value");
        if (document.GetGraph().SetConfig(ten, twenty))
            transaction->AddAction("Constant changed");
    });
    test::Check(read(ten, "value").m_Value == Value::Int32(20), "a change to the document computes it again");

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
        blueprint_editor_utilities::FlowTakenLink(debugger, graph);
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

TEST_CASE(CanvasActionsInactive, "blueprints2.canvas_actions_list_inactive_ones_if_asked_in_as_many_columns_as_they_take", nullptr, test::TestFlags_None)
{
    // Ten gestures, the last one active, listed on a canvas tall enough for them, and on one too
    // short, which takes them in columns.
    test::Session session(UnnamedConfig());
    float height   = 500.0f;
    bool  inactive = false;
    session.AfterEditor = [&]
    {
        CanvasActions actions(true);
        for (int i = 0; i < 10; ++i)
            actions.Gesture("Gesture", "Click", i == 9);
        actions.Draw(ImVec2(100.0f, 100.0f), ImVec2(600.0f, 100.0f + height), inactive);
    };
    auto draw = [&](float canvasHeight, bool withInactive)
    {
        height   = canvasHeight;
        inactive = withInactive;
        session.Frame();
        return test::Capture(session.DrawList);
    };
    const auto text   = ImGui::GetStyle().Colors[ImGuiCol_Text];
    const auto bright = ImGui::ColorConvertFloat4ToU32(text);
    const auto dimmed = ImGui::ColorConvertFloat4ToU32(ImVec4(text.x, text.y, text.z, text.w * ImGui::GetStyle().DisabledAlpha));

    auto count = [](const test::Geometry& geometry, ImU32 color) { return std::count(geometry.Colors.begin(), geometry.Colors.end(), color); };

    const auto active = draw(500.0f, false);
    const auto all    = draw(500.0f, true);
    const auto low    = draw(60.0f, true);
    test::Check(count(active, bright) > 0 && count(active, dimmed) == 0, "by default, only the active gesture shows");
    test::Check(count(all, bright) == count(active, bright) && count(all, dimmed) == 9 * count(active, bright), "asked for the inactive ones too, the nine others show dimmed");
    test::Check(count(low, bright) == count(all, bright) && count(low, dimmed) == count(all, dimmed), "and on a canvas too short for a column of them, all of them still show: %d and %d of %d and %d",
        static_cast<int>(count(low, bright)), static_cast<int>(count(low, dimmed)), static_cast<int>(count(all, bright)), static_cast<int>(count(all, dimmed)));
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
            test::Check(blueprint_editor_utilities::ProblemList::Text(document.GetBlueprint(), problem).starts_with("text: "), "and names the input: %s", blueprint_editor_utilities::ProblemList::Text(document.GetBlueprint(), problem).c_str());
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

    // "loop" is in For Loop, then in While Loop: Down picks the second, and stays there.
    open = [&] { dialog.Open(); };
    session.Run(2);
    for (auto c : std::string_view("loop"))
        ImGui::GetIO().AddInputCharacter(c);
    session.Run(2);
    for (auto key : { ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_Enter })
    {
        test::Key(key, true);
        session.Run(1);
        test::Key(key, false);
        session.Run(2);
    }
    made = graph.FindNode(dialog.GetCreatedNode());
    test::Check(count() == nodes + 4 && made && made->m_Type->m_Name == "crude.WhileLoop", "Down selects the next match, which Enter makes: %s", made ? made->m_Type->m_Name.c_str() : "none");

    open = [&] { dialog.Open(); };
    type("or", ImGuiKey_Escape);
    test::Check(count() == nodes + 4 && !shown, "Esc closes the dialog, making nothing");
}

TEST_CASE(NewDocuments, "blueprints2.a_new_document_has_an_empty_graph", nullptr, test::TestFlags_None)
{
    Document document(ExampleRegistry(), std::make_shared<TypeTable>());
    test::Check(document.GetBlueprint().Graphs().size() == 1 && document.GetGraph().Nodes().empty() && document.GetGraph().GetName() == "Event Graph", "one graph, of no nodes");
    test::Check(!document.m_IsModified && document.m_Undo.empty() && document.m_Redo.empty(), "and no changes to undo");
}

TEST_CASE(GraphLayouts, "blueprints2.each_graph_keeps_its_layout_and_view_while_another_shows", nullptr, test::TestFlags_None)
{
    // The example, with its layout an undo step, and then a function, as an edit not done yet, whose
    // graph shows in an editor of its own.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto  loop     = example.Node("crude.ForLoop");
    ImVec2 loopBefore;
    session.Use([&]
    {
        loopBefore = ed::GetNodePosition(loop);
        auto transaction = document.BeginUndoTransaction("Edit");
        transaction->AddAction("Layout");
    });
    auto function = document.GetBlueprint().AddFunction("Lerp", {});
    auto entry    = function ? function.GetValue()->AddNode("entry:Lerp") : Result<Id>(BlueprintError::NotFound);
    if (!entry)
        return test::Check(false, "the function's entry is added");
    const auto main = document.GetGraph().GetId(), lerp = function.GetValue()->GetId();

    auto position = [](const Document::DocumentState& state, Id node)
    {
        for (auto& saved : state.m_NodesState)
            if (saved.Id.Get() == node)
                return saved.Position;
        return ImVec2(-1.0f, -1.0f);
    };

    auto config = UnnamedConfig();
    auto other  = ed::CreateEditor(&config);
    Document::DocumentState shown;
    ed::SetCurrentEditor(session.Editor);
    document.CaptureView();
    document.m_ShownGraph = lerp;
    ed::SetCurrentEditor(other);
    document.OnMakeCurrent();
    ed::SetNodePosition(entry.GetValue(), ImVec2(100.0f, 50.0f));
    ed::SetViewState({ ImVec2(10.0f, 20.0f), ImVec2(810.0f, 620.0f) });
    document.CaptureView();
    shown = document.BuildDocumentState();
    auto saved = document.Serialize();
    ed::SetCurrentEditor(nullptr);
    test::Check(document.GetGraph().GetId() == lerp && position(shown, entry.GetValue()) == ImVec2(100.0f, 50.0f), "the function's graph shows, the edit that made it kept, its entry where its editor has it");
    test::Check(position(shown, loop) == loopBefore, "and the event graph's For Loop keeps the place it had");

    Document loaded(ExampleRegistry(), std::make_shared<TypeTable>());
    auto read  = loaded.Deserialize(saved);
    auto views = loaded.m_NavigationState.m_Views;
    test::Check(read && views.contains(main) && views.contains(lerp) && views[lerp].VisibleMin == ImVec2(10.0f, 20.0f), "a document saves each graph's view, and loads them back");
    test::Check(loaded.GetGraph().GetId() == main, "and shows its first graph once loaded");
    ed::DestroyEditor(other);
}

TEST_CASE(SignatureEdits, "blueprints2.signatures_change_in_undo_steps", nullptr, test::TestFlags_None)
{
    // A function the example gets, whose signature its entry and return change.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto  added    = document.GetBlueprint().AddFunction("Lerp", {});
    if (!added)
        return test::Check(false, "the function is added");
    const auto lerp = added.GetValue()->GetId();
    auto signature  = [&]() -> const Signature& { return document.GetBlueprint().FindFunction(lerp)->m_Signature; };
    auto keys       = [&](bool input) { std::string text; for (auto& pin : input ? signature().m_Inputs : signature().m_Outputs) text += (text.empty() ? "" : " ") + pin.m_Key; return text; };
    using namespace blueprint_editor_utilities;

    session.Use([&]
    {
        AddSignaturePin(document, lerp, true);
        AddSignaturePin(document, lerp, true);
        AddSignaturePin(document, lerp, false);
    });
    test::Check(keys(true) == "in in1" && keys(false) == "out" && document.m_Undo.size() == 3, "inputs and outputs are added with keys of their own, each an undo step: %s; %s", keys(true).c_str(), keys(false).c_str());

    // T and U, and in1 of type U.
    session.Use([&]
    {
        ChangeSignature(document, lerp, "Add Type Parameters", [](Signature& changed) { changed.m_Parameters = { { "T", {} }, { "U", {} } }; });
        ChangeSignature(document, lerp, "Change Type", [](Signature& changed) { changed.m_Inputs[1].m_Type = TypeVariable{ 1 }; });
        RemoveTypeParameter(document, lerp, 1);
    });
    test::Check(signature().m_Parameters.size() == 2 && document.m_Undo.size() == 5, "a type parameter a pin's type has stays, and makes no undo step");
    session.Use([&] { RemoveTypeParameter(document, lerp, 0); });
    test::Check(signature().m_Parameters.size() == 1 && signature().m_Parameters[0].m_Name == "U" && signature().m_Inputs[1].m_Type.m_Variable == 0, "a type parameter no pin's type has goes, and the variables after it move down");

    auto refused = false;
    session.Use([&]
    {
        auto steps = document.m_Undo.size();
        ChangeSignature(document, lerp, "Rename", [](Signature& changed) { changed.m_Inputs[1].m_Key = "in"; });
        refused = document.m_Undo.size() == steps && keys(true) == "in in1";
        document.Undo();
    });
    test::Check(refused && signature().m_Parameters.size() == 2, "a signature the blueprint refuses makes no step, and undo brings the parameter back");
}

namespace {

// A document whose Start links to a node of the type, which links to a Print.
struct WaitingDocument
{
    WaitingDocument(const char* type, const char* input, Value value)
    {
        auto& graph = m_Document.GetGraph();
        m_Start = graph.AddNode("crude.Start").ValueOr(0);
        m_Node  = graph.AddNode(type).ValueOr(0);
        m_Print = graph.AddNode("crude.Print").ValueOr(0);
        auto& node    = *graph.FindNode(m_Node);
        auto  next    = node.FindPin("completed") ? "completed" : "then";
        auto  made    = graph.Connect(Pin(m_Start, "then"), Pin(m_Node, "enter")).IsOk() && graph.Connect(Pin(m_Node, next), Pin(m_Print, "enter")).IsOk();
        if (!made || !graph.SetValue(Pin(m_Node, input), std::move(value)))
            test::Check(false, "the document of a %s is made", type);
    }

    Id Pin(Id node, std::string_view key) const
    {
        auto found = m_Document.GetGraph().FindNode(node);
        auto pin   = found ? found->FindPin(key) : nullptr;
        return pin ? pin->m_Id : 0;
    }

    Document m_Document{ ExampleRegistry(), std::make_shared<TypeTable>() };
    Id       m_Start = 0;
    Id       m_Node  = 0;
    Id       m_Print = 0;
};

} // namespace

TEST_CASE(SuspendedRuns, "blueprints2.a_run_goes_on_while_its_fiber_waits_and_a_step_skips_the_wait", nullptr, test::TestFlags_None)
{
    // Start, then a Delay of 1 s, then a Print.
    test::Session   session(UnnamedConfig());
    OverlayLog      log;
    WaitingDocument made("crude.Delay", "duration", Value::Float(1.0f));
    auto& document = made.m_Document;

    Debugger debugger;
    auto started = debugger.Start(document, true);
    debugger.Update(document);
    test::Check(started && debugger.IsRunning() && debugger.Waiting() == Wake::At(1.0) && debugger.NextNode() == made.m_Node, "running, the run waits at the Delay");
    debugger.Advance(0.6);
    debugger.Update(document);
    auto waits = debugger.Waiting().has_value();
    debugger.Advance(0.6);
    debugger.Update(document);
    test::Check(waits && !debugger.IsActive() && debugger.GetContext()->Fibers()[0].m_State == FiberState::Done, "the frames' time moves its clock on, and once its time came, it goes on to its end");

    debugger.Start(document, false);
    debugger.Step();
    debugger.Step();
    auto paused = debugger.Waiting();
    debugger.Advance(5.0);
    auto time = debugger.GetContext()->Time();
    debugger.Step();
    test::Check(paused == Wake::At(1.0) && time == 0.0 && debugger.GetContext()->Time() == 1.0 && debugger.NextNode() == made.m_Print, "paused, its clock stays, and a step skips the wait");
}

TEST_CASE(SignalledRuns, "blueprints2.a_fiber_waiting_for_a_signal_goes_on_once_its_sent", nullptr, test::TestFlags_None)
{
    // Start, then a Wait Signal for "go", then a Print.
    test::Session   session(UnnamedConfig());
    OverlayLog      log;
    WaitingDocument made("crude.WaitSignal", "signal", Value::String("go"));
    auto& document = made.m_Document;

    Debugger debugger;
    auto started = debugger.Start(document, false);
    debugger.Step();
    debugger.Step();
    auto waiting = debugger.Waiting();
    debugger.Step();
    auto steps = debugger.StepCount();
    debugger.Signal("stop");
    auto still = debugger.Waiting().has_value();
    debugger.Signal("go");
    debugger.Step();
    test::Check(started && waiting == Wake::OnSignal("go") && steps == 2 && still, "a step can't skip the wait, nor does another signal end it");
    test::Check(!debugger.Waiting() && debugger.IsActive() && debugger.NextNode() == made.m_Print, "its own does, and the run goes on");
}

TEST_CASE(SeededRuns, "blueprints2.each_run_draws_random_numbers_of_its_own", nullptr, test::TestFlags_None)
{
    // Four Randoms in an array, as a run computes it before its first step. Two runs seeded at random
    // draw the same four once in 2^96 runs.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Document      document{ ExampleRegistry(), std::make_shared<TypeTable>() };
    auto& graph = document.GetGraph();
    auto  pin   = [&graph](Id node, std::string_view key) { auto found = graph.FindNode(node); auto pin = found ? found->FindPin(key) : nullptr; return pin ? pin->m_Id : 0; };
    NodeConfig four;
    four.m_Groups["in"] = 4;
    auto start = graph.AddNode("crude.Start").ValueOr(0), print = graph.AddNode("crude.Print").ValueOr(0);
    auto array = graph.AddNode("crude.MakeArray", four).ValueOr(0), text = graph.AddNode("crude.ToString").ValueOr(0);
    auto made  = graph.Connect(pin(start, "then"), pin(print, "enter")).IsOk() && graph.Connect(pin(array, "array"), pin(text, "value")).IsOk() && graph.Connect(pin(text, "text"), pin(print, "text")).IsOk();
    for (int i = 0; i < 4; ++i)
        made = graph.Connect(pin(graph.AddNode("crude.Random").ValueOr(0), "value"), pin(array, "in." + std::to_string(i))).IsOk() && made;
    test::Check(made, "the document is made");

    auto drawn = [&]
    {
        Debugger debugger;
        auto started = debugger.Start(document, false);
        auto read    = started ? debugger.GetContext()->Evaluate(pin(text, "text")) : Result<Watch>(BlueprintError::NotFound);
        return read ? std::string(read.GetValue().m_Value.AsString()) : std::string();
    };
    auto first = drawn(), second = drawn();
    test::Check(!first.empty() && first != second, "two runs draw other numbers: %s and %s", first.c_str(), second.c_str());
}

TEST_CASE(VariableEdits, "blueprints2.variables_change_in_undo_steps_and_go_with_their_nodes", nullptr, test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document  = example.m_Document;
    auto& blueprint = document.GetBlueprint();
    using namespace blueprint_editor_utilities;

    session.Use([&]
    {
        AddVariable(document);
        AddVariable(document);
    });
    auto first = blueprint.FindVariable("Variable1"), second = blueprint.FindVariable("Variable2");
    test::Check(first && second && first->m_Default == Value::Int32(0) && document.m_Undo.size() == 2, "variables are added as Int32s, each of a name of its own, each an undo step");

    session.Use([&]
    {
        ChangeVariable(document, "Variable1", { "Health", Value::Int32(100) }, "Rename Variable");
        ChangeVariable(document, "Health", { "Variable2", Value::Int32(1) }, "Rename Variable");
    });
    auto health = blueprint.FindVariable("Health");
    test::Check(health && health->m_Default == Value::Int32(100) && document.m_Undo.size() == 3, "a change the blueprint refuses makes no step");

    auto added = Result<Id>(BlueprintError::NotFound);
    session.Use([&]
    {
        auto transaction = document.BeginUndoTransaction("Add Node");
        added = document.GetGraph().AddNode("set:Health");
        if (added)
            transaction->AddAction("Set Health added");
    });
    session.Use([&] { RemoveVariable(document, "Health"); });
    test::Check(added && !blueprint.FindVariable("Health") && !document.GetGraph().FindNode(added.ValueOr(0)) && document.m_Undo.size() == 5, "removed, a variable takes its nodes along, in one step");

    session.Use([&] { document.Undo(); });
    test::Check(document.GetBlueprint().FindVariable("Health") && document.GetGraph().FindNode(added.ValueOr(0)), "and undoing it brings both back");
}

TEST_CASE(FunctionNames, "blueprints2.a_function_is_renamed_in_one_undo_step", nullptr, test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto  graph    = Id(0);
    session.Use([&]
    {
        auto transaction = document.BeginUndoTransaction("Add Functions");
        auto first = document.GetBlueprint().AddFunction("Function1", {});
        if (first && document.GetBlueprint().AddFunction("Function2", {}))
            transaction->AddAction("Function1 and Function2 added");
        graph = first ? first.GetValue()->GetId() : 0;
    });
    using blueprint_editor_utilities::RenameFunction;

    session.Use([&]
    {
        RenameFunction(document, graph, "Clamp");
        RenameFunction(document, graph, "Function2");
        RenameFunction(document, graph, "Clamp");
    });
    auto renamed = document.GetBlueprint().FindFunction(graph);
    test::Check(renamed && renamed->m_Name == "Clamp" && document.GetBlueprint().FindGraph(graph)->GetName() == "Clamp" && document.m_Undo.size() == 2, "renamed, it's one undo step; a name it can't take, or its own, makes none");
    session.Use([&] { document.Undo(); });
    auto undone = document.GetBlueprint().FindFunction(graph);
    test::Check(undone && undone->m_Name == "Function1", "and undoing it gives the old name back");
}

TEST_CASE(CustomEventRuns, "blueprints2.a_custom_event_runs_with_its_parameters_defaults_and_is_renamed_in_one_step", nullptr, test::TestFlags_None)
{
    // A Custom Event of an Int32, which Print prints.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto& graph    = document.GetGraph();
    NodeConfig config;
    config.m_Groups["param"] = 1;
    config.m_Types["T0"]     = &Int32Type();
    auto event = Id(0);
    session.Use([&]
    {
        auto transaction = document.BeginUndoTransaction("Add Event");
        auto added = graph.AddNode("crude.CustomEvent", config), print = graph.AddNode("crude.Print");
        if (added && print && graph.Connect(example.Pin(added.GetValue(), "then"), example.Pin(print.GetValue(), "enter")))
            transaction->AddAction("Event added");
        event = added.ValueOr(0);
    });

    Debugger debugger;
    auto started = debugger.Start(document, false, "Event");
    auto value   = started ? debugger.GetContext()->Evaluate(example.Pin(event, "param.0")) : Result<Watch>(BlueprintError::NotFound);
    test::Check(started && debugger.NextNode() == event && value && value.GetValue().m_Value == Value::Int32(0), "a run of the event starts at its node, its parameter of its type's default");

    session.Use([&]
    {
        blueprint_editor_utilities::RenameEvent(document, event, "Spawn");
        blueprint_editor_utilities::RenameEvent(document, event, "Spawn");
    });
    test::Check(document.GetGraph().FindNode(event)->m_Type->EventName(document.GetGraph().FindNode(event)->m_Config) == "Spawn" && document.m_Undo.size() == 2, "renamed, the event is one undo step, and its own name makes none");
    test::Check(debugger.Start(document, false, "Spawn") && !debugger.Start(document, false, "Event"), "and runs by its new name only");
}

TEST_CASE(FunctionRuns, "blueprints2.a_function_runs_from_its_entry_and_gives_its_results", nullptr, test::TestFlags_None)
{
    // Echo(x: Int32) -> y: Int32 returns x; Lerp is generic.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    session.Use([&]
    {
        Signature echo;
        echo.m_Inputs  = { { "x", "X", Int32Type() } };
        echo.m_Outputs = { { "y", "Y", Int32Type() } };
        Signature lerp;
        lerp.m_Parameters = { { "T", {} } };
        lerp.m_Inputs     = { { "a", "A", TypeVariable{ 0 } } };
        auto transaction = document.BeginUndoTransaction("Add Functions");
        auto added = document.GetBlueprint().AddFunction("Echo", echo), generic = document.GetBlueprint().AddFunction("Lerp", lerp);
        if (!added || !generic)
            return;
        auto& body  = *added.GetValue();
        auto  entry = body.AddNode("entry:Echo"), ret = body.AddNode("return:Echo"), other = generic.GetValue()->AddNode("entry:Lerp");
        auto  pin   = [&body](Id node, const char* key) { return body.FindNode(node)->FindPin(key)->m_Id; };
        if (entry && ret && other && body.Connect(pin(entry.GetValue(), "then"), pin(ret.GetValue(), "enter")) && body.Connect(pin(entry.GetValue(), "x"), pin(ret.GetValue(), "y")))
            transaction->AddAction("Echo and Lerp added");
    });

    Debugger debugger;
    auto called = debugger.Call(document, true, "Echo");
    for (int i = 0; i < 10 && debugger.IsRunning(); ++i)
        debugger.Update(document);
    auto fibers = called ? debugger.GetContext()->Fibers() : std::vector<FiberInfo>();
    test::Check(called && !debugger.IsActive() && fibers.size() == 1 && fibers[0].m_State == FiberState::Done && fibers[0].m_Results == std::vector<Value>{ Value::Int32(0) }, "called with its input's default, Echo gives it back");
    test::Check(!debugger.Call(document, true, "Lerp") && !debugger.GetContext(), "a generic function doesn't run");
}

TEST_CASE(FunctionDeletes, "blueprints2.a_function_goes_with_the_calls_of_it_in_one_undo_step", nullptr, test::TestFlags_None)
{
    // Clamp, called from the example's graph.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto  graph    = Id(0), call = Id(0);
    session.Use([&]
    {
        auto transaction = document.BeginUndoTransaction("Add Function");
        auto added = document.GetBlueprint().AddFunction("Clamp", {});
        auto made  = added ? document.GetGraph().AddNode("call:Clamp") : Result<Id>(BlueprintError::NotFound);
        if (made && added.GetValue()->AddNode("entry:Clamp"))
            transaction->AddAction("Clamp added, and called");
        graph = added ? added.GetValue()->GetId() : 0;
        call  = made.ValueOr(0);
    });

    session.Use([&] { blueprint_editor_utilities::RemoveFunction(document, graph); });
    test::Check(!document.GetBlueprint().FindGraph(graph) && !document.GetGraph().FindNode(call) && document.m_Undo.size() == 2, "deleted, a function takes the calls of it along, in one step");
    session.Use([&] { document.Undo(); });
    test::Check(document.GetBlueprint().FindFunction(graph) && document.GetGraph().FindNode(call), "and undoing it brings both back");
}

TEST_CASE(EventParameterRenames, "blueprints2.a_custom_events_parameter_is_renamed_in_one_undo_step", nullptr, test::TestFlags_None)
{
    // A Custom Event of two parameters.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    NodeConfig config;
    config.m_Groups["param"] = 2;
    auto event = Id(0);
    session.Use([&]
    {
        auto transaction = document.BeginUndoTransaction("Add Event");
        auto added = document.GetGraph().AddNode("crude.CustomEvent", config);
        if (added)
            transaction->AddAction("Event added");
        event = added.ValueOr(0);
    });
    const auto pin  = example.Pin(event, "param.1");
    auto       name = [&document, pin] { auto found = document.GetGraph().FindPin(pin); return found ? found->m_Name : std::string(); };

    session.Use([&]
    {
        blueprint_editor_utilities::RenamePin(document, pin, "Count");
        blueprint_editor_utilities::RenamePin(document, pin, "Count");
    });
    test::Check(name() == "Count" && document.m_Undo.size() == 2, "renamed, a parameter is one undo step, and its own name makes none: %s", name().c_str());

    session.Use([&] { blueprint_editor_utilities::RenamePin(document, pin, ""); });
    test::Check(name() == "Param 1" && document.m_Undo.size() == 3 && document.GetGraph().FindNode(event)->m_Config.m_Names.empty(), "an empty name gives back the declared one: %s", name().c_str());
    session.Use([&] { blueprint_editor_utilities::RenamePin(document, pin, ""); });
    test::Check(document.m_Undo.size() == 3, "and again makes no step");

    session.Use([&] { document.Undo(); });
    test::Check(name() == "Count", "undone, it has its name again: %s", name().c_str());
}

TEST_CASE(RunArgumentsTyped, "blueprints2.a_run_takes_the_arguments_typed_for_it", nullptr, test::TestFlags_None)
{
    // Spawn(Who: String, Times: Int32), and Echo<T: Int32 | Float>(x: T) -> y: T, which returns x.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto  spawn    = Id(0), print = Id(0), entry = Id(0), echoGraph = Id(0);
    session.Use([&]
    {
        auto& graph = document.GetGraph();
        NodeConfig config;
        config.m_Data["event"]   = Value::String("Spawn");
        config.m_Groups["param"] = 2;
        config.m_Types["T0"]     = &StringType();
        config.m_Types["T1"]     = &Int32Type();
        config.m_Names           = { { "param.0", "Who" }, { "param.1", "Times" } };
        Signature echo;
        echo.m_Parameters = { { "T", { &Int32ValueType(), &FloatValueType() } } };
        echo.m_Inputs     = { { "x", "X", TypeVariable{ 0 } } };
        echo.m_Outputs    = { { "y", "Y", TypeVariable{ 0 } } };
        auto transaction = document.BeginUndoTransaction("Add Spawn and Echo");
        auto added = graph.AddNode("crude.CustomEvent", config), printed = graph.AddNode("crude.Print");
        auto function = document.GetBlueprint().AddFunction("Echo", echo);
        if (!added || !printed || !function)
            return;
        auto& body = *function.GetValue();
        auto  from = body.AddNode("entry:Echo"), to = body.AddNode("return:Echo");
        auto  pin  = [&body](Id node, const char* key) { return body.FindNode(node)->FindPin(key)->m_Id; };
        if (graph.Connect(example.Pin(added.GetValue(), "then"), example.Pin(printed.GetValue(), "enter")) && graph.Connect(example.Pin(added.GetValue(), "param.0"), example.Pin(printed.GetValue(), "text"))
            && from && to && body.Connect(pin(from.GetValue(), "then"), pin(to.GetValue(), "enter")) && body.Connect(pin(from.GetValue(), "x"), pin(to.GetValue(), "y")))
            transaction->AddAction("Spawn and Echo added");
        spawn = added.ValueOr(0), print = printed.ValueOr(0), entry = from.ValueOr(0), echoGraph = body.GetId();
    });

    auto& blueprint = document.GetBlueprint();
    auto  types     = [&blueprint](const RunArguments& run)
    {
        std::string text;
        for (size_t i = 0; i < run.m_Names.size(); ++i)
        {
            auto type = run.TypeOf(blueprint.GetTypes(), i);
            text += (i ? ", " : "") + run.m_Names[i] + ": " + (type ? type->m_Name : "?") + " = " + (i < run.m_Values.size() ? ValueText(run.m_Values[i]) : "");
        }
        return text;
    };
    auto run = RunArguments::Of(document, spawn);
    if (!run)
        return test::Check(false, "Spawn's node takes arguments");
    run->Settle(blueprint);
    test::Check(!run->m_Call && run->m_Name == "Spawn" && run->Asks() && types(*run) == "Who: String = , Times: Int32 = 0" && run->IsReady(blueprint.GetTypes()), "an event takes its parameters, by their names, of their types' defaults: %s", types(*run).c_str());
    test::Check(!RunArguments::Of(document, print) && !RunArguments::Of(document, example.Node("crude.Start"))->Asks(), "a node of no event takes none, and Start asks for none");

    Debugger debugger;
    run->m_Values = { Value::String("Bob"), Value::Int32(3) };
    auto started = debugger.Start(document, false, "Spawn", run->m_Values);
    auto who     = started ? debugger.GetContext()->Evaluate(example.Pin(spawn, "param.0")) : Result<Watch>(BlueprintError::NotFound);
    test::Check(started && who && who.GetValue().m_Value == Value::String("Bob"), "a run of the event starts with the values typed");
    const Value wrong[] = { Value::Int32(1), Value::Int32(3) };
    test::Check(!debugger.Start(document, false, "Spawn", wrong) && !debugger.GetContext(), "and one of a value of another type doesn't start");

    // Echo is in its own graph.
    document.m_ShownGraph = echoGraph;
    auto echo = RunArguments::Of(document, entry);
    if (!echo)
        return test::Check(false, "Echo's entry takes arguments");
    echo->Settle(blueprint);
    auto choices = std::string();
    for (auto choice : RunArguments::Choices(blueprint.GetRegistry(), echo->m_Parameters[0]))
        choices += (choices.empty() ? "" : " ") + choice->m_Name;
    test::Check(echo->m_Call && choices == "Float Int32" && echo->m_Chosen[0] == &FloatType() && types(*echo) == "X: Float = 0", "a generic function's type parameter takes the first type it allows, its inputs following: %s; %s", choices.c_str(), types(*echo).c_str());

    echo->m_Values[0]  = Value::Float(2.5f);
    auto kept          = *echo;
    echo->m_Chosen[0]  = &Int32Type();
    echo->Settle(blueprint);
    test::Check(types(*echo) == "X: Int32 = 0", "another type gives its inputs their new types' defaults: %s", types(*echo).c_str());
    echo->m_Chosen[0] = &StringType();
    echo->Settle(blueprint);
    test::Check(echo->m_Chosen[0] == &FloatType(), "a type the parameter doesn't allow goes back to the first it does");

    auto again = RunArguments::Of(document, entry);
    again->Reuse(kept);
    again->Settle(blueprint);
    test::Check(types(*again) == "X: Float = 2.5", "a later run reuses an earlier one's arguments: %s", types(*again).c_str());
    auto other = RunArguments::Of(document, entry);
    other->m_Name = "Other";
    other->Reuse(kept);
    test::Check(other->m_Values.empty(), "but not those of another function's");

    auto called = debugger.Call(document, true, "Echo", again->m_Values);
    for (int i = 0; i < 10 && debugger.IsRunning(); ++i)
        debugger.Update(document);
    auto fibers = called ? debugger.GetContext()->Fibers() : std::vector<FiberInfo>();
    test::Check(called && fibers.size() == 1 && fibers[0].m_State == FiberState::Done && fibers[0].m_Results == std::vector<Value>{ Value::Float(2.5f) }, "called with them, a generic function runs, typed by them");
}

TEST_CASE(ClicksPastPopups, "blueprints2.a_click_outside_a_popup_closes_it_and_reaches_what_it_is_over", nullptr, test::TestFlags_None)
{
    // A node, and a popup over the canvas away from it.
    test::Session session(UnnamedConfig());
    session.BeforeHost = [] { blueprint_editor_utilities::ClosePopupsOnClickOutside(); };
    ImVec2 node;
    bool   open = false, shown = false;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        ed::BeginNode(1);
        ImGui::TextUnformatted("Node");
        ed::EndNode();
        node = ed::CanvasToScreen(ed::GetNodePosition(1) + ImVec2(10.0f, 10.0f));
        ed::Suspend();
        if (std::exchange(open, false))
            ImGui::OpenPopup("##menu");
        ImGui::SetNextWindowPos(ImVec2(600.0f, 400.0f), ImGuiCond_Appearing);
        shown = ImGui::BeginPopup("##menu");
        if (shown)
        {
            ImGui::TextUnformatted("Menu");
            ImGui::EndPopup();
        }
        ed::Resume();
    };
    session.Run(3);
    open = true;
    session.Run(3);
    const auto opened = shown;
    session.Click(node);
    const auto selected = session.Use([] { return ed::IsNodeSelected(1); });
    test::Check(opened && !shown && selected, "the click closes the popup, and selects the node");

    open = true;
    session.Run(3);
    session.Click(ImVec2(620.0f, 410.0f));
    test::Check(shown, "a click in the popup leaves it open");
}

TEST_CASE(TypeParameterRenames, "blueprints2.a_type_parameter_is_renamed_in_one_undo_step", nullptr, test::TestFlags_None)
{
    // Echo<T: Int32 | Float>(x: T), whose entry's pin is of T.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto  graph    = Id(0), entry = Id(0);
    session.Use([&]
    {
        Signature echo;
        echo.m_Parameters = { { "T", { &Int32ValueType(), &FloatValueType() } } };
        echo.m_Inputs     = { { "x", "X", TypeVariable{ 0 } } };
        auto transaction = document.BeginUndoTransaction("Add Echo");
        auto added = document.GetBlueprint().AddFunction("Echo", echo);
        auto made  = added ? added.GetValue()->AddNode("entry:Echo") : Result<Id>(BlueprintError::NotFound);
        if (made)
            transaction->AddAction("Echo added");
        graph = added ? added.GetValue()->GetId() : 0;
        entry = made.ValueOr(0);
    });
    auto& blueprint = document.GetBlueprint();
    auto  pinType   = [&blueprint, graph, entry] { return blueprint.FindGraph(graph)->FindNode(entry)->FindPin("x")->m_DecidedType; };
    test::Check(blueprint_editor_utilities::TypeParameterText(blueprint, pinType()) == "T, a type parameter of Echo; allows Int32, Float", "a pin of the parameter tells it: %s", blueprint_editor_utilities::TypeParameterText(blueprint, pinType()).c_str());
    const auto color = blueprint_editor_utilities::TypeColor(pinType());

    session.Use([&]
    {
        blueprint_editor_utilities::RenameTypeParameter(document, graph, 0, "Item");
        blueprint_editor_utilities::RenameTypeParameter(document, graph, 0, "Item");
        blueprint_editor_utilities::RenameTypeParameter(document, graph, 0, "Int32");
    });
    test::Check(blueprint.FindFunction(graph)->m_Signature.m_Parameters[0].m_Name == "Item" && document.m_Undo.size() == 2, "renamed, it is one undo step; its own name, and a refused one, make none");
    test::Check(blueprint_editor_utilities::TypeParameterText(blueprint, pinType()).starts_with("Item, a type parameter of Echo"), "its pins tell the new name");

    session.Use([&] { blueprint_editor_utilities::RenameFunction(document, graph, "Repeat"); });
    const auto renamed = blueprint_editor_utilities::TypeColor(pinType());
    session.Use([&] { blueprint_editor_utilities::RenameTypeParameter(document, graph, 0, "T"); });
    const auto back = blueprint_editor_utilities::TypeColor(pinType());
    test::Check(back.x == color.x && back.y == color.y && back.z == color.z && (renamed.x != color.x || renamed.y != color.y || renamed.z != color.z), "a parameter's color follows its own name, not its function's");
    test::Check(blueprint_editor_utilities::TypeParameterText(blueprint, &Int32Type()).empty(), "and a type that isn't a parameter tells nothing of one");
}

TEST_CASE(SignatureSteps, "blueprints2.a_signature_edit_is_one_undo_step_and_none_when_it_changes_nothing", nullptr, test::TestFlags_None)
{
    // Echo(x: Int32).
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto  graph    = Id(0);
    session.Use([&]
    {
        Signature echo;
        echo.m_Inputs = { { "x", "X", Int32Type() } };
        auto transaction = document.BeginUndoTransaction("Add Echo");
        auto added = document.GetBlueprint().AddFunction("Echo", echo);
        if (added && added.GetValue()->AddNode("entry:Echo"))
            transaction->AddAction("Echo added");
        graph = added ? added.GetValue()->GetId() : 0;
    });
    auto& blueprint = document.GetBlueprint();
    auto  signature = [&blueprint, graph]() -> const Signature& { return blueprint.FindFunction(graph)->m_Signature; };
    auto  steps     = [&document] { return document.m_Undo.size(); };

    const auto before = steps();
    session.Use([&]
    {
        blueprint_editor_utilities::RenameSignaturePin(document, graph, true, "x", "Value");
        blueprint_editor_utilities::RenameSignaturePin(document, graph, true, "x", "Value");
    });
    test::Check(signature().m_Inputs[0].m_Name == "Value" && steps() == before + 1, "a renamed input is one step, and its own name none");

    session.Use([&]
    {
        blueprint_editor_utilities::AddTypeParameter(document, graph);
        blueprint_editor_utilities::AddTypeParameter(document, graph);
        blueprint_editor_utilities::ToggleAllowedType(document, graph, 0, &Int32ValueType());
        blueprint_editor_utilities::ToggleAllowedType(document, graph, 0, &FloatValueType());
        blueprint_editor_utilities::ToggleAllowedType(document, graph, 0, &Int32ValueType());
    });
    auto& parameters = signature().m_Parameters;
    test::Check(parameters.size() == 2 && parameters[0].m_Name == "T" && parameters[1].m_Name == "U" && parameters[0].m_Allowed == std::vector<const ValueType*>{ &FloatValueType() } && steps() == before + 6, "parameters are T, then U, and a toggle adds a type or takes it away");

    session.Use([&] { blueprint_editor_utilities::RetypeSignaturePin(document, graph, true, "x", TypeVariable{ 0 }); });
    auto user = blueprint_editor_utilities::TypeParameterUser(signature(), 0);
    test::Check(signature().m_Inputs[0].m_Type == TypeTerm(TypeVariable{ 0 }) && user && user->m_Key == "x" && !blueprint_editor_utilities::TypeParameterUser(signature(), 1), "retyped, the input is of T, which it uses, and U is used by none");
    session.Use([&] { blueprint_editor_utilities::RemoveTypeParameter(document, graph, 0); });
    test::Check(signature().m_Parameters.size() == 2 && steps() == before + 7, "so T can't go");

    auto choices = blueprint_editor_utilities::SignatureTypeChoices(blueprint.GetRegistry(), signature());
    auto has = [&choices](const char* name, bool array) { return std::any_of(choices.begin(), choices.end(), [&](const blueprint_editor_utilities::SignatureTypeChoice& choice) { return choice.m_Name == name && choice.m_Array == array; }); };
    test::Check(has("Int32", false) && has("T", false) && has("U", true) && has("Float", true) && !has("Array", false), "the choices are the value types of no arguments, the parameters, and arrays of both");

    session.Use([&] { blueprint_editor_utilities::RemoveSignaturePin(document, graph, true, "x"); });
    test::Check(signature().m_Inputs.empty() && steps() == before + 8, "and a removed input is one step");
}

TEST_CASE(RightClicksPastMenus, "blueprints2.a_right_click_outside_a_menu_closes_it_and_opens_the_menu_it_is_over", nullptr, test::TestFlags_None)
{
    // A node whose right-click opens a menu, as the example's do, and the canvas's own menu.
    test::Session session(UnnamedConfig());
    session.BeforeHost = [] { blueprint_editor_utilities::ClosePopupsOnClickOutside(); };
    ImVec2 node;
    int    nodeMenus = 0, backgroundMenus = 0;
    bool   shown = false;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        ed::BeginNode(1);
        ImGui::TextUnformatted("Node");
        ed::EndNode();
        node = ed::CanvasToScreen(ed::GetNodePosition(1) + ImVec2(10.0f, 10.0f));
        ed::NodeId id;
        const auto nodeMenu = ed::ShowNodeContextMenu(&id);
        backgroundMenus += ed::ShowBackgroundContextMenu() ? 1 : 0;
        ed::Suspend();
        if (nodeMenu)
        {
            ++nodeMenus;
            ImGui::OpenPopup("##node-menu");
        }
        shown = ImGui::BeginPopup("##node-menu");
        if (shown)
        {
            ImGui::TextUnformatted("Menu");
            ImGui::EndPopup();
        }
        ed::Resume();
    };
    session.Run(3);
    session.Click(node, 1);
    test::Check(nodeMenus == 1 && shown, "a right-click on the node opens its menu");

    // A hand's click moves the mouse a little.
    test::MouseMove(ImVec2(700.0f, 500.0f));
    session.Run(2);
    test::MouseButton(1, true);
    session.Run(2);
    test::MouseMove(ImVec2(702.0f, 499.0f));
    session.Run(2);
    test::MouseButton(1, false);
    session.Run(2);
    test::Check(!shown && backgroundMenus == 1, "a right-click on the canvas that moves 2 px closes it and opens the canvas's: %d", backgroundMenus);
}

TEST_CASE(ValueEditsEnd, "blueprints2.a_value_edit_ends_on_a_click_elsewhere_keeping_what_was_typed", nullptr, test::TestFlags_None)
{
    // A text and a number with their editors, in the host window above the canvas.
    using blueprint_editor_utilities::DrawValueWithEditor;
    test::Session session(UnnamedConfig());
    auto   text = Value::String("Done"), number = Value::Int32(10);
    ImRect textRect, numberRect;
    session.BeforeEditor = [&]
    {
        ImGui::SetCursorScreenPos(ImVec2(100.0f, 100.0f));
        ImGui::PushID(1);
        DrawValueWithEditor(1, text);
        ImGui::PopID();
        textRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::SetCursorScreenPos(ImVec2(100.0f, 160.0f));
        ImGui::PushID(2);
        DrawValueWithEditor(2, number);
        ImGui::PopID();
        numberRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    };
    auto type = [&](const char* typed)
    {
        for (auto c = typed; *c; ++c)
            ImGui::GetIO().AddInputCharacter(static_cast<unsigned>(*c));
        session.Run(2);
    };
    const auto elsewhere = ImVec2(700.0f, 500.0f);
    session.Run(3);
    const auto chip = textRect.GetWidth();

    session.Click(textRect.GetCenter());
    session.Run(2);
    type("hi");
    const auto editing = textRect.GetWidth() > chip;
    session.Click(elsewhere);
    test::Check(editing && text.AsString() == "hi" && textRect.GetWidth() < chip, "a text's editor opens with its field active, and a click elsewhere keeps what was typed and closes it: \"%s\"", std::string(text.AsString()).c_str());

    session.Click(textRect.GetCenter());
    session.Run(2);
    type("x");
    test::Key(ImGuiKey_Escape, true);
    session.Run(1);
    test::Key(ImGuiKey_Escape, false);
    session.Run(2);
    test::Check(text.AsString() == "hi" && textRect.GetWidth() < chip, "Escape closes it without the edit: \"%s\"", std::string(text.AsString()).c_str());

    session.Click(numberRect.GetCenter());
    session.Run(2);
    const auto plus = ImVec2(numberRect.Max.x - numberRect.GetHeight() * 0.5f, numberRect.GetCenter().y);
    session.Click(plus);
    session.Click(plus);
    const auto open = numberRect.GetWidth() > numberRect.GetHeight() * 3.0f;
    session.Click(elsewhere);
    test::Check(open && number.AsInt32() == 12 && numberRect.GetWidth() < numberRect.GetHeight() * 3.0f, "a number's + steps it with the editor kept open, and a click elsewhere closes it: %d", number.AsInt32());

    session.Click(numberRect.GetCenter());
    session.Run(2);
    type("7");
    session.Click(elsewhere);
    test::Check(number.AsInt32() == 7, "a number typed is kept too: %d", number.AsInt32());
}

TEST_CASE(EmptyValueBoxes, "blueprints2.an_empty_values_box_is_half_a_line_wide_and_takes_a_click", nullptr, test::TestFlags_None)
{
    // An empty text with its editor, in the host window above the canvas.
    using blueprint_editor_utilities::DrawValueWithEditor;
    test::Session session(UnnamedConfig());
    auto   empty = Value::String("");
    ImRect emptyRect;
    session.BeforeEditor = [&]
    {
        ImGui::SetCursorScreenPos(ImVec2(100.0f, 100.0f));
        DrawValueWithEditor(1, empty);
        emptyRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    };
    session.Run(3);

    const auto box  = emptyRect.GetSize();
    const auto half = box.x == box.y * 0.5f && box.x > 0.0f;
    session.Click(emptyRect.GetCenter());
    session.Run(2);
    ImGui::GetIO().AddInputCharacter('a');
    session.Run(2);
    session.Click(ImVec2(700.0f, 500.0f));
    test::Check(half && empty.AsString() == "a", "an empty text's box is half as wide as high, %g x %g, and a click opens its editor: \"%s\"",
        box.x, box.y, std::string(empty.AsString()).c_str());
}

TEST_CASE(StructEdits, "blueprints2.a_structs_fields_are_edited_in_turn_in_one_editor", nullptr, test::TestFlags_None)
{
    // A Vector2 with its editor, in the host window above the canvas: x is typed into, then y
    // stepped twice by its +, which keeps the editor open.
    using blueprint_editor_utilities::DrawValueWithEditor;
    test::Session session(UnnamedConfig());
    auto   registry = ExampleRegistry();
    auto   head     = registry->FindValueType("example.Vector2");
    auto   vector   = head->Default(head->AsType());
    ImRect vectorRect;
    session.BeforeEditor = [&]
    {
        ImGui::SetCursorScreenPos(ImVec2(100.0f, 100.0f));
        DrawValueWithEditor(1, vector);
        vectorRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    };
    const auto elsewhere = ImVec2(700.0f, 500.0f);
    session.Run(3);

    const auto line = vectorRect.GetHeight();
    session.Click(vectorRect.GetCenter());
    session.Run(2);
    for (auto c : { '1', '.', '5' })
        ImGui::GetIO().AddInputCharacter(static_cast<unsigned>(c));
    session.Run(2);
    const auto rows = vectorRect.GetHeight() > line * 1.5f;
    const auto row  = (vectorRect.GetHeight() - ImGui::GetStyle().ItemSpacing.y) * 0.5f;
    const auto plus = ImVec2(vectorRect.Max.x - row * 0.5f, vectorRect.Max.y - row * 0.5f);
    session.Click(plus);
    session.Click(plus);
    const auto open   = vectorRect.GetHeight() > line * 1.5f;
    const auto fields = vector.AsStruct();
    test::Check(rows && open && fields[0].AsFloat() == 1.5f && fields[1].AsFloat() == 2.0f, "a Vector2's editor has a row for each field, and keeps x typed and y stepped: %s",
        blueprint_editor_utilities::ValueText(vector).c_str());
    session.Click(elsewhere);
    test::Check(vectorRect.GetHeight() == line, "and a click elsewhere closes it: %g high, %g before", vectorRect.GetHeight(), line);
}

TEST_CASE(CommentTexts, "blueprints2.a_comments_text_is_edited_in_one_undo_step", nullptr, test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto  comment  = Id(0);
    session.Use([&]
    {
        auto transaction = document.BeginUndoTransaction("Add Comment");
        auto added = document.GetGraph().AddNode("crude.Comment");
        if (added)
            transaction->AddAction("Comment added");
        comment = added.ValueOr(0);
    });
    auto text = [&] { auto found = document.GetGraph().FindNode(comment); return found ? blueprint_editor_utilities::CommentText(*found) : std::string(); };
    test::Check(comment && text() == "Comment" && document.m_Undo.size() == 1, "a new comment says \"Comment\": %s", text().c_str());

    session.Use([&]
    {
        blueprint_editor_utilities::SetCommentText(document, comment, "Greets");
        blueprint_editor_utilities::SetCommentText(document, comment, "Greets");
    });
    test::Check(text() == "Greets" && document.m_Undo.size() == 2, "its text is one undo step, and the same text none: %s", text().c_str());
    session.Use([&] { document.Undo(); });
    test::Check(text() == "Comment", "undone, it says what it said: %s", text().c_str());
}

TEST_CASE(RerouteInserts, "blueprints2.a_reroute_put_into_a_link_takes_the_sources_type_and_the_links_cast", nullptr, test::TestFlags_None)
{
    using namespace blueprint_editor_utilities;
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    const auto sum = example.Node("crude.Add"), print = example.Node("crude.Print"), loop = example.Node("crude.ForLoop");
    const auto result = example.Pin(sum, "result"), text = example.Pin(print, "text");
    const auto body   = example.Pin(loop, "body"), enter = example.Pin(print, "enter");
    const auto cast   = example.LinkBetween(result, text), flowing = example.LinkBetween(body, enter);
    if (!cast || cast->m_Cast.empty() || !flowing)
        return test::Check(false, "the example's sum reaches the Print's text through a cast, and its loop's body the Print");
    const auto castLink = cast->m_Id, flowLink = flowing->m_Id;

    // Into the cast link, at (600, 200).
    const auto steps   = document.m_Undo.size();
    const auto reroute = session.Use([&] { return InsertReroute(document, castLink, ImVec2(600.0f, 200.0f)); });
    const auto in = example.Pin(reroute, "in"), out = example.Pin(reroute, "out");
    auto first  = example.LinkBetween(result, in);
    auto second = example.LinkBetween(out, text);
    test::Check(first && first->m_Cast.empty() && second && !second->m_Cast.empty() && !example.LinkBetween(result, text) && document.GetGraph().TypeText(out) == "Int32" && document.m_Undo.size() == steps + 1,
        "put into the link, a reroute takes the sum's Int32 plainly and casts it on: one undo step");
    const auto center = session.Use([&] { return ed::GetNodePosition(reroute) + ImVec2(RerouteSize(), RerouteSize()) * 0.5f; });
    test::Check(ImFabs(center.x - 600.0f) < 0.5f && ImFabs(center.y - 200.0f) < 0.5f, "centered where it was put: (%g, %g)", center.x, center.y);

    // Into the loop's body, a flow reroute.
    const auto flow  = session.Use([&] { return InsertReroute(document, flowLink, ImVec2(500.0f, 100.0f)); });
    const auto found = document.GetGraph().FindNode(flow);
    test::Check(found && found->m_Type->m_Name == "crude.FlowReroute" && example.LinkBetween(body, example.Pin(flow, "in")) && example.LinkBetween(example.Pin(flow, "out"), enter),
        "a flow link takes a flow reroute");
}

TEST_CASE(RerouteRemovals, "blueprints2.a_dissolved_reroute_joins_its_links_and_a_removed_one_takes_them_away", nullptr, test::TestFlags_None)
{
    using namespace blueprint_editor_utilities;
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    const auto sum = example.Node("crude.Add"), print = example.Node("crude.Print"), loop = example.Node("crude.ForLoop");
    const auto result = example.Pin(sum, "result"), text = example.Pin(print, "text");
    const auto body   = example.Pin(loop, "body"), enter = example.Pin(print, "enter");
    const auto cast   = example.LinkBetween(result, text), flowing = example.LinkBetween(body, enter);
    if (!cast || cast->m_Cast.empty() || !flowing)
        return test::Check(false, "the example's sum reaches the Print's text through a cast, and its loop's body the Print");
    const auto castLink = cast->m_Id, flowLink = flowing->m_Id;

    // A reroute in the cast link, dissolved as a deletion does it.
    const auto reroute = session.Use([&] { return InsertReroute(document, castLink, ImVec2(600.0f, 200.0f)); });
    session.Use([&]
    {
        auto transaction = document.BeginUndoTransaction("Dissolve Reroute");
        DissolveReroute(document.GetGraph(), reroute);
        transaction->AddAction("dissolved");
    });
    auto joined = example.LinkBetween(result, text);
    test::Check(reroute && !document.GetGraph().FindNode(reroute) && joined && !joined->m_Cast.empty(), "dissolved, a reroute gives the cast link back");

    // A flow reroute in the loop's body, removed with its links, and the removal undone.
    const auto flow = session.Use([&] { return InsertReroute(document, flowLink, ImVec2(500.0f, 100.0f)); });
    session.Use([&] { RemoveReroute(document, flow); });
    const auto links = document.GetGraph().Links();
    test::Check(flow && !document.GetGraph().FindNode(flow) && std::none_of(links.begin(), links.end(), [body, enter](const Link& link) { return link.m_From == body || link.m_To == enter; }),
        "removed, it takes its links with it");
    session.Use([&] { document.Undo(); });
    test::Check(document.GetGraph().FindNode(flow) && example.LinkBetween(body, example.Pin(flow, "in")) && example.LinkBetween(example.Pin(flow, "out"), enter),
        "and undone, it is back with them");
}

TEST_CASE(RerouteFlows, "blueprints2.a_run_goes_past_a_flow_reroute_and_flows_both_its_links", nullptr, test::TestFlags_None)
{
    using namespace blueprint_editor_utilities;
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    const auto print = example.Node("crude.Print"), loop = example.Node("crude.ForLoop");
    const auto body  = example.Pin(loop, "body"), enter = example.Pin(print, "enter");
    const auto flowing = example.LinkBetween(body, enter);
    if (!flowing)
        return test::Check(false, "the example's loop's body leads into the Print");

    const auto flow = session.Use([&] { return InsertReroute(document, flowing->m_Id, ImVec2(500.0f, 100.0f)); });
    auto into = example.LinkBetween(body, example.Pin(flow, "in"));
    auto onto = example.LinkBetween(example.Pin(flow, "out"), enter);

    Debugger debugger;
    debugger.Start(document, false);
    debugger.Step();
    debugger.Step();
    const auto chain = FlowChain(document.GetGraph(), debugger.TakenLink());
    test::Check(debugger.CurrentNode() == loop && debugger.NextNode() == print, "the run paused after For Loop, with Print next, past the reroute");
    test::Check(into && onto && chain == std::vector<Id>{ into->m_Id, onto->m_Id }, "the link it took is the one into the reroute, which flows with the one out of it");
}

TEST_CASE(RerouteDirections, "blueprints2.links_run_through_a_reroute_along_the_axis_nearest_their_way", nullptr, test::TestFlags_None)
{
    using blueprint_editor_utilities::RerouteDirection;
    const ImVec2 center(300.0f, 300.0f), left[] = { ImVec2(100.0f, 300.0f) }, right[] = { ImVec2(500.0f, 300.0f) };
    const ImVec2 above[] = { ImVec2(300.0f, 100.0f) }, below[] = { ImVec2(300.0f, 500.0f) };
    const ImVec2 rightAbove[] = { ImVec2(600.0f, 250.0f) }, rightBelow[] = { ImVec2(600.0f, 350.0f) };
    auto is = [](ImVec2 way, float x, float y) { return way.x == x && way.y == y; };
    test::Check(is(RerouteDirection(center, left, right), 1.0f, 0.0f) && is(RerouteDirection(center, right, left), -1.0f, 0.0f), "fed from the left for the right, links run right; the other way, left");
    test::Check(is(RerouteDirection(center, above, below), 0.0f, 1.0f) && is(RerouteDirection(center, below, above), 0.0f, -1.0f), "from above to below, they run down; the other way, up");
    test::Check(is(RerouteDirection(center, rightBelow, rightAbove), 0.0f, -1.0f) && is(RerouteDirection(center, rightAbove, rightBelow), 0.0f, 1.0f), "turning back at it, they pass through it a quarter turn round, up or down as they go");
    test::Check(is(RerouteDirection(center, right, {}), -1.0f, 0.0f) && is(RerouteDirection(center, {}, above), 0.0f, -1.0f) && is(RerouteDirection(center, {}, {}), 1.0f, 0.0f), "one side linked, they run as from or toward it; unlinked, right");
}

TEST_CASE(PinGrowths, "blueprints2.a_node_grows_by_a_pin_of_its_group_and_loses_one_over_its_minimum", nullptr, test::TestFlags_None)
{
    using namespace blueprint_editor_utilities;
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document  = example.m_Document;
    auto& blueprint = document.GetBlueprint();
    auto  node      = [&document](Id id) { return document.GetGraph().FindNode(id); };
    const auto sum  = example.Node("crude.Add");

    const auto growth = GrowthOf(blueprint, *node(sum));
    test::Check(growth && growth.m_Group == "in" && growth.m_Direction == Direction::Input && growth.m_Enabled, "Add grows by an input of its group");
    test::Check(!RemovalOf(blueprint, *node(sum), *node(sum)->FindPin("in.0")), "at its minimum of two, it loses none");

    session.Use([&] { GrowPins(document, sum, growth); });
    const auto third   = node(sum)->FindPin("in.2");
    const auto removal = third ? RemovalOf(blueprint, *node(sum), *third) : PinRemoval();
    test::Check(third && removal && removal.m_Index == 2, "grown, it has a third input, which it can lose");
    session.Use([&] { RemovePin(document, sum, removal); });
    test::Check(!node(sum)->FindPin("in.2"), "and loses it");
}

TEST_CASE(PinDrops, "blueprints2.a_link_dropped_on_a_nodes_plus_adds_a_pin_linked", nullptr, test::TestFlags_None)
{
    using namespace blueprint_editor_utilities;
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto  node     = [&document](Id id) { return document.GetGraph().FindNode(id); };
    const auto sum = example.Node("crude.Add"), value = example.Pin(example.Node("crude.Constant"), "value");

    // The constant dropped on Add's "+": a third input, linked to it, in one undo step.
    const auto steps = document.m_Undo.size();
    test::Check(CanLinkNewGroupPin(document.GetGraph(), sum, "in", value).IsOk() && !node(sum)->FindPin("in.2"), "a link to a third input fits, tried on a copy of the graph");
    session.Use([&] { AddGroupPinLinked(document, sum, "in", value); });
    const auto third = node(sum)->FindPin("in.2");
    test::Check(third && example.LinkBetween(value, third->m_Id) && document.m_Undo.size() == steps + 1, "dropped on it, the link adds the third input, linked, in one undo step");
    test::Check(!CanLinkNewGroupPin(document.GetGraph(), sum, "in", example.Pin(example.Node("crude.Print"), "then")), "a flow output's link doesn't fit a new input of Add");
}

TEST_CASE(OrphanMending, "blueprints2.an_orphan_is_split_again_its_links_moved_or_its_links_broken", nullptr, test::TestFlags_None)
{
    using namespace blueprint_editor_utilities;
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    auto& graph    = document.GetGraph();
    const auto value = example.Pin(example.Node("crude.Constant"), "value");

    // A function's input renamed: a call keeps the old one, an orphan, which moves its link to the new.
    Signature before, after;
    before.m_Inputs = { { "t", "T", Int32Type() } };
    after.m_Inputs  = { { "u", "U", Int32Type() } };
    auto function   = document.GetBlueprint().AddFunction("Scale", before);
    auto call       = session.Use([&] { return graph.AddNode("call:Scale"); });
    const auto t    = call ? example.Pin(call.GetValue(), "t") : 0;
    auto linked     = graph.Connect(value, t);
    auto renamed    = function ? document.GetBlueprint().SetSignature(function.GetValue()->GetId(), after) : Result<void>(BlueprintError::NotFound);
    const auto u    = call ? example.Pin(call.GetValue(), "u") : 0;
    test::Check(function && call && linked && renamed && graph.NodeOfOrphan(t), "renamed, the call's old input stays, an orphan, with its link");
    const auto steps = document.m_Undo.size();
    session.Use([&] { MoveOrphanLinks(document, t, u); });
    auto moved = example.LinkBetween(value, u);
    test::Check(moved && moved->m_Active && !graph.NodeOfOrphan(t) && document.m_Undo.size() == steps + 1, "its link moves to the new input, active, and the orphan goes: one undo step");

    // A split output's field linked on, then recombined: split again, the field is a pin again.
    auto make   = session.Use([&] { return graph.AddNode("make:example.Vector2"); });
    auto other  = session.Use([&] { return graph.AddNode("make:example.Vector2"); });
    auto split  = make ? graph.SplitPin(make.GetValue(), "struct", true) : Result<void>(BlueprintError::NotFound);
    const auto field = make ? example.Pin(make.GetValue(), "struct.x") : 0;
    auto fed    = other ? graph.Connect(field, example.Pin(other.GetValue(), "x")) : Result<Id>(BlueprintError::NotFound);
    auto whole  = make ? graph.SplitPin(make.GetValue(), "struct", false) : Result<void>(BlueprintError::NotFound);
    auto node   = make ? graph.FindNode(make.GetValue()) : nullptr;
    auto orphan = node ? node->FindOrphan(field) : nullptr;
    test::Check(split && fed && whole && orphan && SplitAgainKey(*node, *orphan) == "struct", "recombined, its field is an orphan its struct pin splits again into");

    // The call's input renamed again, an orphan of its own: a node's orphans' links broken, then all.
    Signature again;
    again.m_Inputs = { { "v", "V", Int32Type() } };
    auto renamedAgain = document.GetBlueprint().SetSignature(function.GetValue()->GetId(), again);
    session.Use([&] { RemoveOrphans(document, make.GetValue()); });
    test::Check(renamedAgain && graph.FindNode(make.GetValue())->m_Orphans.empty() && !graph.FindLink(fed.GetValue()) && graph.NodeOfOrphan(u), "a node's orphans' links broken, it has none, and the call keeps its own");
    session.Use([&] { RemoveOrphans(document, 0); });
    test::Check(std::all_of(graph.Nodes().begin(), graph.Nodes().end(), [](const Node& node) { return node.m_Orphans.empty(); }), "and all the graph's broken, no node has one");
}

namespace {

// The document's nodes as boxes of their titles, a comment's with a group, so that the editor knows
// their sizes.
void DrawNodes(test::Session& session, const Document& document)
{
    session.DefaultContent = [&document](int)
    {
        for (auto& node : document.GetGraph().Nodes())
        {
            ed::BeginNode(node.m_Id);
            ImGui::TextUnformatted(node.m_Type->m_Title.c_str());
            if (blueprint_editor_utilities::IsComment(node))
                ed::Group(ImVec2(320.0f, 180.0f));
            ed::EndNode();
        }
    };
}

ImRect Bounds(std::span<const Id> nodes)
{
    ImRect bounds(ImVec2(FLT_MAX, FLT_MAX), ImVec2(-FLT_MAX, -FLT_MAX));
    for (auto node : nodes)
    {
        bounds.Add(ed::GetNodePosition(node));
        bounds.Add(ed::GetNodePosition(node) + ed::GetNodeSize(node));
    }
    return bounds;
}

} // namespace

TEST_CASE(ClipboardPastes, "blueprints2.copied_nodes_paste_with_their_links_around_a_point_in_one_undo_step", nullptr, test::TestFlags_None)
{
    using namespace blueprint_editor_utilities;
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    DrawNodes(session, document);
    session.Run(2);

    // The loop, the Add of its index and 10, and the 10: two links among them.
    const Id   nodes[] = { example.Node("crude.ForLoop"), example.Node("crude.Add"), example.Node("crude.Constant") };
    const auto text    = session.Use([&] { return CopyNodes(document, nodes); });
    const auto count   = document.GetGraph().Nodes().size(), links = document.GetGraph().Links().size(), steps = document.m_Undo.size();
    auto pasted = session.Use([&] { return PasteNodes(document, text, ImVec2(900.0f, 500.0f), "Paste"); });
    if (!pasted || pasted.GetValue().size() != 3)
        return test::Check(false, "the three nodes paste: %s", pasted ? "" : std::string(pasted.GetError().Message()).c_str());
    const auto copies = pasted.GetValue();
    test::Check(document.GetGraph().Nodes().size() == count + 3 && document.GetGraph().Links().size() == links + 2 && document.m_Undo.size() == steps + 1,
        "they paste with the two links among them, in one undo step");
    auto copy = document.GetGraph().FindNode(copies[0]);
    auto last = copy && copy->FindPin("last") ? document.GetGraph().FindValue(copy->FindPin("last")->m_Id) : nullptr;
    test::Check(last && *last == Value::Int32(4) && example.LinkBetween(example.Pin(copies[0], "index"), example.Pin(copies[1], "in.0")), "the loop's copy counts to 4 too, and gives its index to the Add's copy");

    session.Run(2);
    session.Use([&]
    {
        const auto offset = ed::GetNodePosition(copies[0]) - ed::GetNodePosition(nodes[0]);
        auto kept = true, selected = true;
        for (size_t i = 0; i < copies.size(); ++i)
        {
            const auto moved = ed::GetNodePosition(copies[i]) - ed::GetNodePosition(nodes[i]);
            kept     = kept && ImFabs(moved.x - offset.x) < 0.5f && ImFabs(moved.y - offset.y) < 0.5f;
            selected = selected && ed::IsNodeSelected(copies[i]) && !ed::IsNodeSelected(nodes[i]);
        }
        const auto center = Bounds(copies).GetCenter();
        test::Check(kept && ImFabs(center.x - 900.0f) <= 1.0f && ImFabs(center.y - 500.0f) <= 1.0f, "they lie as their originals do, around the point: (%g, %g)", center.x, center.y);
        test::Check(selected && ed::GetSelectedObjectCount() == 3, "and they're the selection");
    });

    auto refused = session.Use([&] { return PasteNodes(document, "Start, For Loop and Print", ImVec2(900.0f, 500.0f), "Paste"); });
    test::Check(!refused && document.GetGraph().Nodes().size() == count + 3 && document.m_Undo.size() == steps + 1, "text that holds no nodes pastes none: %s", refused ? "" : std::string(refused.GetError().Message()).c_str());
    session.Use([&] { document.Undo(); });
    test::Check(document.GetGraph().Nodes().size() == count && document.GetGraph().Links().size() == links, "undone, the copies are gone");
}

TEST_CASE(ClipboardDuplicates, "blueprints2.duplicates_lie_beside_their_nodes_selected_in_one_undo_step", nullptr, test::TestFlags_None)
{
    using namespace blueprint_editor_utilities;
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    DrawNodes(session, document);
    session.Run(2);

    const Id   nodes[] = { example.Node("crude.Add"), example.Node("crude.Constant") };
    const auto steps   = document.m_Undo.size();
    auto duplicated = session.Use([&] { return DuplicateNodes(document, nodes, ImVec2(32.0f, 32.0f)); });
    if (!duplicated || duplicated.GetValue().size() != 2)
        return test::Check(false, "the two nodes duplicate: %s", duplicated ? "" : std::string(duplicated.GetError().Message()).c_str());
    const auto copies = duplicated.GetValue();
    session.Run(2);
    session.Use([&]
    {
        auto beside = true;
        for (size_t i = 0; i < copies.size(); ++i)
            beside = beside && ed::GetNodePosition(copies[i]) == ed::GetNodePosition(nodes[i]) + ImVec2(32.0f, 32.0f) && ed::IsNodeSelected(copies[i]);
        test::Check(beside && document.m_Undo.size() == steps + 1, "each lies 32 units right and down of its node, selected, in one undo step");
    });
    auto ten = document.GetGraph().FindNode(copies[1]);
    test::Check(ten && ten->m_Config == document.GetGraph().FindNode(nodes[1])->m_Config && example.LinkBetween(example.Pin(copies[1], "value"), example.Pin(copies[0], "in.1")),
        "the 10's copy is a 10 too, linked into the Add's copy");
}

TEST_CASE(ClipboardComments, "blueprints2.a_selected_comment_takes_the_nodes_in_it_along", nullptr, test::TestFlags_None)
{
    using namespace blueprint_editor_utilities;
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Example       example(session);
    auto& document = example.m_Document;
    DrawNodes(session, document);

    // A comment around the Add and the 10.
    const auto sum = example.Node("crude.Add"), ten = example.Node("crude.Constant");
    const auto comment = session.Use([&]
    {
        auto added = document.GetGraph().AddNode("crude.Comment");
        ed::SetNodePosition(added.ValueOr(0), ImVec2(230.0f, 240.0f));
        ed::SetGroupSize(added.ValueOr(0), ImVec2(420.0f, 220.0f));
        return added.ValueOr(0);
    });
    session.Run(2);

    const auto taken = session.Use([&]
    {
        ed::SelectNode(comment);
        return SelectedNodesWithContent(document.GetGraph());
    });
    auto has = [&taken](Id node) { return std::find(taken.begin(), taken.end(), node) != taken.end(); };
    test::Check(taken.size() == 3 && has(comment) && has(sum) && has(ten), "a selected comment takes the Add and the 10 in it, and no other node: %d nodes", static_cast<int>(taken.size()));

    auto duplicated = session.Use([&] { return DuplicateNodes(document, taken, ImVec2(0.0f, 300.0f)); });
    session.Run(2);
    auto copy = ed::NodeState();
    const auto sized = duplicated && duplicated.GetValue().size() == 3 && session.Use([&] { return ed::GetNodeState(duplicated.GetValue()[0], &copy); });
    test::Check(sized && copy.GroupSize == ImVec2(420.0f, 220.0f) && copy.Position == ImVec2(230.0f, 540.0f), "its copy is as large, 300 units below: (%g, %g)", copy.Position.x, copy.Position.y);
}
