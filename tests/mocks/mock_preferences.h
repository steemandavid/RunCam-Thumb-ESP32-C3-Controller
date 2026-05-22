#pragma once

#include "storage/settings_store.h"
#include <map>
#include <string>
#include <cstring>

class MockPreferences : public IPreferences {
public:
    bool begin(const char* name, bool readOnly) override {
        (void)name; (void)readOnly;
        return true;
    }
    void end() override {}
    bool isKey(const char* key) override {
        return store_.find(key) != store_.end();
    }
    int32_t getInt(const char* key, int32_t defaultValue = 0) override {
        auto it = store_.find(key);
        if (it != store_.end()) return it->second;
        return defaultValue;
    }
    size_t putInt(const char* key, int32_t value) override {
        store_[key] = value;
        return sizeof(int32_t);
    }
    bool remove(const char* key) override {
        return store_.erase(key) > 0;
    }

    void clear() { store_.clear(); }
    size_t count() const { return store_.size(); }

private:
    std::map<std::string, int32_t> store_;
};
