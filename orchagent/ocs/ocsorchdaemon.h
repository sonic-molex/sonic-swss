#pragma once
#include "orchdaemon.h"

class OcsOrchDaemon : public OrchDaemon
{
public:
    OcsOrchDaemon(DBConnector *applDb, DBConnector *configDb, DBConnector *stateDb, DBConnector *chassisAppDb, ZmqServer *zmqServer);
    bool init() override;

private:
    DBConnector *m_applDb;
    DBConnector *m_configDb;
};
