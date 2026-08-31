#include "ob/Document.h"
#include "ob/FileFormat.h"

namespace ob {

bool Document::saveToFile(const std::string& path) const {
    ObnSaveResult r = obnSave(*this, path);
    return r.success;
}

bool Document::loadFromFile(const std::string& path) {
    ObnLoadResult r = obnLoad(*this, path);
    return r.success;
}

} // namespace ob
