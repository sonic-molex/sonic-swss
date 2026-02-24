#pragma once

#include "dbconnector.h"
#include "orch.h"
#include "producerstatetable.h"

#include <map>
#include <string>
#include <memory>

namespace swss {

class OtnMgr : public Orch
{
public:
    OtnMgr(DBConnector *cfgDb, DBConnector *appDb, DBConnector *stateDb, const std::vector<std::string> &tableNames, const std::map<std::string, std::string> &tableMaps);

    using Orch::doTask;
private:
    DBConnector *m_appl_db;
    DBConnector *m_state_db;
    const std::map<std::string, std::string> &m_tableMaps;
    std::map<std::string, std::shared_ptr<ProducerStateTable>> m_appTables;

    void doTask(Consumer &consumer);
    void writeConfigToAppDb(std::shared_ptr<ProducerStateTable> &table, const std::string &alias, const std::string &field, const std::string &value);
    void writeConfigToAppDb(std::shared_ptr<ProducerStateTable> &table, const std::string &alias, std::vector<FieldValueTuple> &field_values);
};

}
