#pragma once

#include "dbconnector.h"
#include "orch.h"
#include "producerstatetable.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace swss {

class OtnMgr : public Orch
{
public:
    OtnMgr(DBConnector *cfgDb, DBConnector *appDb, const std::vector<std::string> &tableNames, const std::map<std::string, std::string> &tableMaps);

    using Orch::doTask;
private:
    DBConnector *m_appl_db;
    std::map<std::string, std::string> m_tableMaps;
    std::map<std::string, std::shared_ptr<ProducerStateTable>> m_appTables;

    void doTask(Consumer &consumer);
    void writeConfigToAppDb(std::shared_ptr<ProducerStateTable> &table, const std::string &alias, const std::vector<FieldValueTuple> &field_values);
};

}
