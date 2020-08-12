# include <application.h>
# define IMGUI_DEFINE_MATH_OPERATORS
# include <imgui_internal.h>

# include "crude_blueprint.h"


const char* Application_GetName()
{
    return "Blueprints2";
}

void Application_Initialize()
{
    using namespace crude_blueprint;

    IdGenerator g;

    Context executor;

    ConstBoolNode valueA{g};
    valueA.m_Bool.m_Value = true;

    ToStringNode toStringNode{g};
    toStringNode.m_Value.m_Link = &valueA.m_Bool;

    PrintNode truePrintNode{g};
    truePrintNode.m_String.m_Value = "It's true!";
    truePrintNode.m_String.m_Link = &toStringNode.m_String;
    toStringNode.m_Exit.m_Link = &truePrintNode.m_Enter;

    PrintNode falsePrintNode{g};
    falsePrintNode.m_String.m_Value = "It's false!";


    BranchNode branchNode{g};
    branchNode.m_Condition.m_Link = &valueA.m_Bool;
    branchNode.m_True.m_Link = &toStringNode.m_Enter;
    branchNode.m_False.m_Link = &falsePrintNode.m_Enter;

    FlipFlopNode flipFlopNode{g};

    DoNNode doNNode{g};
    doNNode.m_Exit.m_Link = &flipFlopNode.m_Enter;
    doNNode.m_N.m_Value = 5;
    toStringNode.m_Value.m_Link = &doNNode.m_Counter;

    flipFlopNode.m_A.m_Link = &branchNode.m_Enter;
    flipFlopNode.m_B.m_Link = &falsePrintNode.m_Enter;

    executor.Execute(doNNode.m_Enter);

}

void Application_Finalize()
{
}

void Application_Frame()
{
}