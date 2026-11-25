#pragma once
#include <QString>
#include <filesystem>

void exportAdapterChecks(const std::filesystem::path& samples, const QString& qualifier);
void exportUiChecks(const std::filesystem::path& samples, const QString& qualifier);
