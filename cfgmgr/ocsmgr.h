#pragma once

#include "dbconnector.h"
#include "orch.h"
#include "producerstatetable.h"

#include <map>
#include <vector>
#include <string>
#include <memory>

namespace swss {

class OcsMgr : public Orch
{
public:
    OcsMgr(DBConnector *cfgDb, DBConnector *appDb, DBConnector *stateDb, const std::vector<std::string> &tableNames, const std::map<std::string, std::string> &tableMaps);

    using Orch::doTask;
private:
    DBConnector *m_appl_db;
    DBConnector *m_state_db;
    const std::map<std::string, std::string> &m_tableMaps;
    std::map<std::string, std::shared_ptr<ProducerStateTable>> m_appTables;
    std::map<std::string, std::vector<std::string>> m_deviceExtraMap;

    void doTask(Consumer &consumer);
    bool writeConfigToAppDb(std::shared_ptr<ProducerStateTable> &table, const std::string &alias, const std::string &field, const std::string &value);
    bool writeConfigToAppDb(std::shared_ptr<ProducerStateTable> &table, const std::string &alias, std::vector<FieldValueTuple> &field_values);
    bool isOnline(const std::string &alias);
    bool queryDeviceStatus(const std::string &port);
    bool loadDeviceExtraMap();
    std::string queryDeviceName(const std::string &port);
    std::vector<std::string> parsePorts(const std::string &alias);
};

}
