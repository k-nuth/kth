// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <kth/node.hpp>

using namespace kth;

namespace {

int process_id() {
#if defined(_WIN32)
    return ::_getpid();
#else
    return ::getpid();
#endif
}

// Writes a flat config file for the duration of a test.
struct config_file {
    explicit config_file(std::string const& tag, std::string const& text)
        : path_(std::filesystem::temp_directory_path() / ("kth_cfg_" + tag + "_" + std::to_string(process_id()) + ".cfg"))
    {
        std::ofstream(path_) << text << "\n";
    }
    ~config_file() { std::error_code ec; std::filesystem::remove(path_, ec); }
    std::filesystem::path const& path() const { return path_; }
private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("sv2 listener settings survive network selection and config loading", "[configuration tests][parser][sv2 template_provider]") {
    node::parser parser(domain::config::network::mainnet);
    char const* argv[]{"kth", "--network", "regtest", "--sv2.enabled=true",
        "--sv2.bind", "::1", "--sv2.port", "18442"};
    std::ostringstream error;
    REQUIRE(parser.parse(8, argv, error));
    CHECK(parser.configured.sv2.enabled);
    CHECK(parser.configured.sv2.bind == "::1");
    CHECK(parser.configured.sv2.port == 18442u);

    config_file file("sv2", "sv2.enabled=true\nsv2.bind=127.0.0.2\nsv2.port=0");
    node::parser from_file(domain::config::network::regtest);
    REQUIRE(from_file.parse_from_file(file.path(), error));
    CHECK(from_file.configured.sv2.enabled);
    CHECK(from_file.configured.sv2.bind == "127.0.0.2");
    CHECK(from_file.configured.sv2.port == 0u);

    node::configuration const copied(from_file.configured);
    CHECK(copied.sv2.enabled);
    CHECK(copied.sv2.bind == "127.0.0.2");
    CHECK(copied.sv2.port == 0u);

    auto const path = file.path().string();
    node::parser overridden(domain::config::network::mainnet);
    // Neither value below is a default: the bind address can only have come
    // from the file, and the port only from the command line.
    char const* override_argv[]{"kth", "--network", "regtest", "--config", path.c_str(),
        "--sv2.port", "18443"};
    REQUIRE(overridden.parse(7, override_argv, error));
    CHECK(overridden.configured.sv2.enabled);
    CHECK(overridden.configured.sv2.bind == "127.0.0.2");
    CHECK(overridden.configured.sv2.port == 18443u);
}

TEST_CASE("sv2 Noise settings parse and default to plaintext", "[configuration tests][parser][sv2 noise]") {
    node::parser defaults(domain::config::network::regtest);
    char const* none[]{"kth", "--network", "regtest", "--sv2.enabled=true"};
    std::ostringstream error;
    REQUIRE(defaults.parse(4, none, error));
    // Plaintext stays the default, as it was before Noise.
    CHECK_FALSE(defaults.configured.sv2.noise);
    CHECK(defaults.configured.sv2.static_key_file.empty());
    CHECK(defaults.configured.sv2.authority_key_file.empty());
    CHECK(defaults.configured.sv2.certificate_file.empty());
    CHECK(defaults.configured.sv2.certificate_validity_seconds == 2592000u);

    config_file file("sv2_noise", "sv2.enabled=true\nsv2.noise=true\n"
        "sv2.static_key_file=keys/provider.key\nsv2.authority_key_file=/secure/authority.key\n"
        "sv2.authority_public_key=9bXiEd8boQVhq7WddEcERUL5tyyJVFYdU8th3HfbNXK3Yw6GRXh\n"
        "sv2.certificate_validity_seconds=86400");
    node::parser from_file(domain::config::network::regtest);
    REQUIRE(from_file.parse_from_file(file.path(), error));
    auto const& sv2 = from_file.configured.sv2;
    CHECK(sv2.noise);
    CHECK(sv2.static_key_file == "keys/provider.key");
    CHECK(sv2.authority_key_file == "/secure/authority.key");
    CHECK(sv2.authority_public_key == "9bXiEd8boQVhq7WddEcERUL5tyyJVFYdU8th3HfbNXK3Yw6GRXh");
    CHECK(sv2.certificate_validity_seconds == 86400u);

    node::parser offline(domain::config::network::regtest);
    char const* argv[]{"kth", "--network", "regtest", "--sv2.enabled=true", "--sv2.noise=true",
        "--sv2.certificate_file", "provider.cert"};
    REQUIRE(offline.parse(7, argv, error));
    CHECK(offline.configured.sv2.certificate_file == "provider.cert");
}
