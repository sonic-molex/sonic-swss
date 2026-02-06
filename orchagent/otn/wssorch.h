#pragma once

#include "objectorch.h"

class WssOrch: public ObjectOrch
{
public:
    WssOrch(DBConnector *db, const std::vector<std::string> &table_names);
    void addExtraAttrsOnCreate(const std::string &key, std::vector<sai_attribute_t> &attrs) override;
};

class WssSpecPowerOrch: public ObjectOrch
{
public:
    WssSpecPowerOrch(DBConnector *db, const std::vector<std::string> &table_names);
    bool addNameOnCreate() const override { return false; }
    void addExtraAttrsOnCreate(const std::string &key, std::vector<sai_attribute_t> &attrs) override;

private:
    std::shared_ptr<DBConnector> m_cfgDb;
    std::unique_ptr<Table> m_parentWssCfgTable;  // CONFIG_DB OTN_WSS
};
