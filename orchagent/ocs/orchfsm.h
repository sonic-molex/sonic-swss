#pragma once
#include <atomic>
#include <map>

enum OrchState {
    ORCH_STATE_NOT_READY,
    ORCH_STATE_READY,
    ORCH_STATE_WORK,
    ORCH_STATE_PAUSE,
};

class OrchFSM
{
public:
    static OrchFSM &getInstance();
    static void setState(OrchState state);
    static OrchState getState();

private:
    OrchFSM() = default;
    ~OrchFSM() = default;

    //TODO: Should sync state with line card later.
    //std::atomic<OrchState> m_state = { ORCH_STATE_NOT_READY };
    std::atomic<OrchState> m_state = { ORCH_STATE_WORK };
};
