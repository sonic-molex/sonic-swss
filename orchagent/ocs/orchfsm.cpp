#include "orchfsm.h"

OrchFSM &OrchFSM::getInstance()
{
    static OrchFSM m_orchStateInst;
    return m_orchStateInst;
}

void OrchFSM::setState(OrchState state)
{
    getInstance().m_state = state;
}

OrchState OrchFSM::getState()
{
    return getInstance().m_state;
}
