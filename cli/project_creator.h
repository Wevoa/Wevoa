#pragma once

#include <filesystem>
#include <string>

#include "utils/file_writer.h"

namespace wevoaweb {

class ProjectCreator {
  public:
    explicit ProjectCreator(FileWriter writer = {});

    std::filesystem::path createProject(const std::string& projectName) const;

  private:
    std::filesystem::path resolveTargetPath(const std::string& projectName) const;
    static std::string layoutTemplate(const std::string& displayName);
    static std::string mainTemplate(const std::string& displayName);
    static std::string indexTemplate();
    static std::string docsTemplate();
    static std::string contactTemplate();
    static std::string navTemplate();
    static std::string styleTemplate();
    static std::string configTemplate(const std::string& displayName);
    static std::string readmeTemplate(const std::string& displayName);

    FileWriter writer_;
};

}  // namespace wevoaweb
