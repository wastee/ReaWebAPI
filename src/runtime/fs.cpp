#include "runtime/fs.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <set>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <stdio.h>
#include <cerrno>
#else
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#endif

namespace reaweb {
fs::path existing_local_path(const fs::path& base, const Json& input) {
  if (!input.is_string()) throw Error("INVALID_PATH", "Expected a local path");
  const auto value = input.get<std::string>();
  if (value.empty() || value.size() > 32768 || value.find('\0') != std::string::npos || value.find("://") != std::string::npos)
    throw Error("INVALID_PATH", "Expected a local path without a URL scheme or NUL");
  auto path = fs::u8path(value); std::error_code error;
  path = fs::canonical(path.is_absolute() ? path : base / path, error);
  if (error) throw Error("FILE_NOT_FOUND", "Local path is unavailable: " + value);
  return path;
}
namespace {
fs::path path_arg(const fs::path& base, const Json& arg) {
  if (!arg.is_string()) throw Error("INVALID_PATH", "Expected a filesystem path");
  const auto text = arg.get<std::string>();
  if (text.empty() || text.size() > 32768 || text.find('\0') != std::string::npos || text.find("://") != std::string::npos)
    throw Error("INVALID_PATH", "Expected a local path, without a URL scheme or NUL");
  auto path = fs::u8path(text);
  return fs::weakly_canonical(path.is_absolute() ? path : base / path);
}
Json options(const Json& args, size_t index, const std::set<std::string>& allowed) {
  if (args.size() <= index) return Json::object();
  const auto& value = args[index];
  if (!value.is_object()) throw Error("INVALID_ARGUMENT", "Expected file options");
  for (const auto& item : value.items()) if (!allowed.count(item.key())) throw Error("INVALID_ARGUMENT", "Unknown file option: " + item.key());
  return value;
}
void text_encoding(const std::string& text) {
  try { (void)Json(text).dump(); }
  catch (const Json::exception&) { throw Error("FILE_ENCODING", "File is not valid UTF-8; read it as binary"); }
}
std::string encoding(const Json& opt) {
  const auto name = opt.value("encoding", std::string("utf8"));
  if (name != "utf8" && name != "binary") throw Error("INVALID_ARGUMENT", "encoding must be utf8 or binary");
  return name;
}
Json stat(const fs::path& path) {
  const auto info = fs::status(path);
  return {{"path", path.u8string()}, {"exists", fs::exists(info)},
    {"type", fs::is_regular_file(info) ? "file" : fs::is_directory(info) ? "directory" : "other"},
    {"size", fs::is_regular_file(info) ? Json(fs::file_size(path)) : Json(nullptr)}};
}
fs::path move_path(const fs::path& base, const Json& arg) {
  // Resolve parents, but never follow the final entry when moving or replacing it.
  if (!arg.is_string()) throw Error("INVALID_PATH", "Expected a filesystem path");
  const auto text = arg.get<std::string>();
  if (text.empty() || text.size() > 32768 || text.find('\0') != std::string::npos || text.find("://") != std::string::npos)
    throw Error("INVALID_PATH", "Expected a local path without a URL scheme or NUL");
  auto path = fs::u8path(text);
  if (path.filename().empty() || path.filename() == "." || path.filename() == ".." ||
      (!path.is_absolute() && path.has_root_path()))
    throw Error("INVALID_PATH", "Expected an absolute or entry-relative file path including its filename");
  if (!path.is_absolute()) path = base / path;
  return fs::weakly_canonical(path.parent_path()) / path.filename();
}
void commit_move(const fs::path& source, const fs::path& target, bool overwrite, std::error_code& error) {
#ifdef _WIN32
  if (!MoveFileExW(source.c_str(), target.c_str(), overwrite ? MOVEFILE_REPLACE_EXISTING : 0))
    error = std::error_code(GetLastError(), std::system_category());
#else
  if (overwrite) fs::rename(source, target, error);
  else {
#ifdef __APPLE__
    if (renamex_np(source.c_str(), target.c_str(), RENAME_EXCL) == 0) return;
    if (errno != ENOTSUP && errno != EINVAL) { error = std::error_code(errno, std::generic_category()); return; }
#else
    if (syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD, target.c_str(), 1 /* RENAME_NOREPLACE */) == 0) return;
    if (errno != ENOSYS && errno != EINVAL && errno != EOPNOTSUPP) { error = std::error_code(errno, std::generic_category()); return; }
#endif
    fs::create_hard_link(source, target, error);
    if (!error) {
      if (!fs::remove(source, error) || error)
        throw Error("FILE_MOVE_PARTIAL", "Destination was created but source could not be removed: " + source.u8string());
    }
  }
#endif
}
Json move_file(const fs::path& base, const Json& args) {
  const auto opt = options(args, 2, {"overwrite"});
  const bool overwrite = opt.value("overwrite", false);
  const auto source = move_path(base, args[0]), target = move_path(base, args[1]);
  const auto source_status = fs::symlink_status(source), target_status = fs::symlink_status(target);
  if (fs::is_symlink(source_status) || fs::is_symlink(target_status))
    throw Error("INVALID_PATH", "moveFile does not accept symbolic links as source or destination files");
  if (!fs::is_regular_file(source_status)) throw Error("FILE_NOT_FOUND", "Source is not a regular file");
  const auto bytes = fs::file_size(source);
  const auto result = Json{{"source", source.u8string()}, {"path", target.u8string()}, {"bytes", bytes}};
  if (fs::exists(target_status)) {
    if (!fs::is_regular_file(target_status)) throw Error("INVALID_PATH", "Destination must name a file, not a directory");
    if (fs::equivalent(source, target)) return result;
    if (!overwrite) throw Error("FILE_EXISTS", "Destination exists; pass overwrite: true to replace it");
  }
  std::error_code error;
  commit_move(source, target, overwrite, error);
  if (!error) return result;
#ifdef _WIN32
  const bool cross_device = error.value() == ERROR_NOT_SAME_DEVICE;
#else
  const bool cross_device = error == std::errc::cross_device_link;
#endif
  if (!cross_device) {
    if (error == std::errc::file_exists) throw Error("FILE_EXISTS", "Destination already exists");
    throw Error("FILE_IO", "Cannot move file: " + error.message());
  }
  // Stage on the destination volume. Publish only a complete copy, then remove the source.
  static std::atomic<unsigned> counter{0};
  fs::path staging;
  for (int attempt = 0; attempt < 32; ++attempt) {
    staging = target.parent_path() / (".reaweb-move-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++counter));
    if (fs::create_directory(staging)) break;
    staging.clear();
  }
  if (staging.empty()) throw Error("FILE_IO", "Cannot reserve a move staging directory");
  const auto temporary = staging / "file";
  try {
    fs::copy_file(source, temporary);
    fs::last_write_time(temporary, fs::last_write_time(source));
    error.clear(); commit_move(temporary, target, overwrite, error);
    if (error) throw Error(error == std::errc::file_exists ? "FILE_EXISTS" : "FILE_IO", "Cannot publish moved file: " + error.message());
    if (!fs::remove(source, error) || error)
      throw Error("FILE_MOVE_PARTIAL", "Destination was created but source could not be removed: " + source.u8string());
  } catch (...) {
    std::error_code ignored; fs::remove(temporary, ignored); fs::remove(staging, ignored); throw;
  }
  fs::remove(staging, error);
  return result;
}
}
bool is_file_method(const std::string& name) {
  return name == "ReaWeb_ReadFile" || name == "ReaWeb_WriteFile" || name == "ReaWeb_Stat" ||
    name == "ReaWeb_ReadDirectory" || name == "ReaWeb_MakeDirectory" || name == "ReaWeb_MoveFile";
}
Json file_call(const fs::path& base, const std::string& method, const Json& args) {
  try {
    if (method == "ReaWeb_MoveFile") {
      if (!args.is_array() || args.size() < 2 || args.size() > 3) throw Error("INVALID_ARGUMENT", "Expected source, destination and optional move options");
      return move_file(base, args);
    }
    const bool write = method == "ReaWeb_WriteFile";
    if (!args.is_array() || args.size() < (write ? 2u : 1u) || args.size() > (write ? 3u : 2u))
      throw Error("INVALID_ARGUMENT", "Wrong file argument count");
    const auto path = path_arg(base, args[0]);
    const auto opt = options(args, write ? 2 : 1, write ? std::set<std::string>{"encoding", "overwrite"} :
      method == "ReaWeb_ReadFile" ? std::set<std::string>{"encoding"} :
      method == "ReaWeb_MakeDirectory" ? std::set<std::string>{"recursive"} : std::set<std::string>{});
    if (method == "ReaWeb_Stat") return stat(path);
    if (method == "ReaWeb_MakeDirectory") {
      const bool recursive = opt.value("recursive", false);
      return recursive ? fs::create_directories(path) : fs::create_directory(path);
    }
    if (method == "ReaWeb_ReadDirectory") {
      if (!fs::is_directory(path)) throw Error("FILE_NOT_DIRECTORY", "Path is not a directory");
      std::vector<fs::path> names;
      for (const auto& entry : fs::directory_iterator(path)) {
        if (names.size() >= 4096) throw Error("DIRECTORY_LIMIT", "Directory exceeds 4096 entries");
        names.push_back(entry.path());
      }
      std::sort(names.begin(), names.end());
      auto result = Json::array();
      for (const auto& name : names) {
        auto info = stat(name); info["name"] = name.filename().u8string(); result.push_back(std::move(info));
      }
      return result;
    }
    const bool binary = encoding(opt) == "binary";
    if (method == "ReaWeb_ReadFile") {
      if (!fs::is_regular_file(path)) throw Error("FILE_NOT_FOUND", "Path is not a regular file");
      if (fs::file_size(path) > value_limit) throw Error("BUFFER_LIMIT", "File exceeds 16 MiB");
      std::ifstream stream(path, std::ios::binary);
      if (!stream) throw Error("FILE_IO", "Cannot open file");
      std::string data; char block[8192];
      while (stream) {
        stream.read(block, sizeof(block));
        if (data.size() + static_cast<size_t>(stream.gcount()) > value_limit) throw Error("BUFFER_LIMIT", "File exceeds 16 MiB");
        data.append(block, static_cast<size_t>(stream.gcount()));
      }
      if (!stream.eof()) throw Error("FILE_IO", "Cannot read file");
      if (binary) return encode_binary(data.data(), data.size());
      text_encoding(data);
      return data;
    }
    if (!write) throw Error("UNKNOWN_API", "Unknown file operation");
    if (!binary && !args[1].is_string()) throw Error("INVALID_ARGUMENT", "UTF-8 writes require a string");
    const auto data = binary ? decode_binary(args[1]) : args[1].get<std::string>();
    if (data.size() > value_limit) throw Error("BUFFER_LIMIT", "File exceeds 16 MiB");
    if (!binary) text_encoding(data);
    const bool overwrite = opt.value("overwrite", false);
    if (!overwrite && fs::exists(path)) throw Error("FILE_EXISTS", "File exists; pass overwrite: true to replace it");
    static std::atomic<unsigned> counter{0};
    auto temporary = path;
    temporary += ".reaweb-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++counter) + ".tmp";
    try {
      {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream.write(data.data(), static_cast<std::streamsize>(data.size())); stream.flush();
        if (!stream) throw Error("FILE_IO", "Cannot write file");
      }
#ifdef _WIN32
      if (!MoveFileExW(temporary.c_str(), path.c_str(), (overwrite ? MOVEFILE_REPLACE_EXISTING : 0) | MOVEFILE_WRITE_THROUGH))
        throw Error("FILE_IO", "Cannot commit file (destination may exist or be in use)");
#else
      if (overwrite) fs::rename(temporary, path);
      else { fs::create_hard_link(temporary, path); fs::remove(temporary); }
#endif
    } catch (...) { std::error_code ignored; fs::remove(temporary, ignored); throw; }
    return {{"path", path.u8string()}, {"bytes", data.size()}};
  } catch (const fs::filesystem_error& e) { throw Error("FILE_IO", e.what()); }
  catch (const Json::exception&) { throw Error("INVALID_ARGUMENT", "Invalid file option type"); }
}
}
