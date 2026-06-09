#pragma once
#include "orchdaemon.h"

class OtnOrchDaemon : public OrchDaemon
{
public:
    OtnOrchDaemon(DBConnector *applDb, DBConnector *configDb, DBConnector *stateDb, DBConnector *chassisAppDb, ZmqServer *zmqServer);
    bool init() override;

private:
    DBConnector *m_applDb;
    DBConnector *m_configDb;
};
