#pragma once

#include "objectorch.h"

class OcsCrossConnectOrch: public ObjectOrch
{
public:
    OcsCrossConnectOrch(DBConnector *applDb, const std::vector<std::string> &table_names);
    virtual void setFlexCounter(sai_object_id_t id) override;
    void addExtraAttrsOnCreate(const std::string &key, std::vector<sai_attribute_t> &attrs);

    std::vector<std::string> collectInvalidConnections() override;
    void cleanupInvalidConnections(const std::vector<std::string> &invalidKeys) override;
};
