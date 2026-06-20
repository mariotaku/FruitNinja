#include "xml/XmlLoad.h"
#include "util/PathCI.h"
#include "debug/Logger.h"
#include <tinyxml2.h>

namespace FN {

tinyxml2::XMLError LoadXmlCI(tinyxml2::XMLDocument& doc, const std::string& path) {
    tinyxml2::XMLError err = doc.LoadFile(path.c_str());
    if (err != tinyxml2::XML_SUCCESS) {
        std::string ci = Mortar::ResolvePathCI(path.c_str());
        if (!ci.empty()) err = doc.LoadFile(ci.c_str());
    }
    if (err != tinyxml2::XML_SUCCESS) {
        LOG_ERROR("XmlLoad", "failed to load '%s' (err=%d)", path.c_str(), (int)err);
    }
    return err;
}

} // namespace FN
