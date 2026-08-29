#include "Core/Resources.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <ios>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)

namespace {

/// Scratch directory that is wiped on construction and destruction.
class TempDirectory {
 public:
  TempDirectory() : m_root{std::filesystem::temp_directory_path() / "opennomad-resources-test"} {
    std::filesystem::remove_all(m_root);
    std::filesystem::create_directories(m_root);
  }

  ~TempDirectory() {
    std::filesystem::remove_all(m_root);
  }

  TempDirectory(const TempDirectory&) = delete;
  TempDirectory(TempDirectory&&) = delete;
  TempDirectory& operator=(const TempDirectory&) = delete;
  TempDirectory& operator=(TempDirectory&&) = delete;

  [[nodiscard]] const std::filesystem::path& root() const {
    return m_root;
  }

 private:
  std::filesystem::path m_root;
};

void write_file(const std::filesystem::path& path) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream{path, std::ios::binary};
  stream << "data";
}

}  // namespace

TEST_SUITE("Core::Resources") {
  TEST_CASE("resolve_case_insensitive keeps an exact path unchanged") {
    const TempDirectory temp;
    const std::filesystem::path path{temp.root() / "AKG_FNM.3dt"};
    write_file(path);

    CHECK_EQ(App::Resources::resolve_case_insensitive(path).string(), path.string());
  }

  TEST_CASE("resolve_case_insensitive matches a differently-cased file name") {
    const TempDirectory temp;
    const std::filesystem::path path{temp.root() / "akg_fnm.3dt"};
    write_file(path);

    const std::filesystem::path resolved{
        App::Resources::resolve_case_insensitive(temp.root() / "AKG_FNM.3DT")};
    CHECK_EQ(resolved.string(), path.string());
    CHECK(std::filesystem::exists(resolved));
  }

  TEST_CASE("resolve_case_insensitive matches differently-cased directories") {
    const TempDirectory temp;
    const std::filesystem::path path{temp.root() / "MeshES" / "PersOS" / "akg_fnm.3dt"};
    write_file(path);

    const std::filesystem::path resolved{App::Resources::resolve_case_insensitive(
        temp.root() / "MESHES" / "PERSOS" / "AKG_FNM.3DT")};
    CHECK_EQ(resolved.string(), path.string());
    CHECK(std::filesystem::exists(resolved));
  }

  TEST_CASE("SCPTDATA/GRID.SCX resolves an on-disk Grid.SCX fixture") {
    const TempDirectory temp;
    const std::filesystem::path path{temp.root() / "SCPTDATA" / "Grid.SCX"};
    write_file(path);

    const std::filesystem::path resolved{
        App::Resources::resolve_case_insensitive(temp.root() / "SCPTDATA" / "GRID.SCX")};
    CHECK_EQ(resolved.string(), path.string());
    CHECK(std::filesystem::exists(resolved));
  }

  TEST_CASE("resolve_case_insensitive returns the requested path when nothing matches") {
    const TempDirectory temp;
    const std::filesystem::path missing{temp.root() / "NOPE.3dt"};

    CHECK_EQ(App::Resources::resolve_case_insensitive(missing).string(), missing.string());
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)
