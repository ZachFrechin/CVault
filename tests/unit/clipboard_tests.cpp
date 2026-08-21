#include <catch2/catch_test_macros.hpp>

#include "src/tui/clipboard.hpp"

TEST_CASE("Le presse-papiers transmet le texte a l'implementation injectee", "[clipboard]")
{
    std::string received;
    Clipboard clipboard([&](std::string_view text, std::string& error) {
        received = std::string(text);
        error.clear();
        return true;
    });
    std::string error;

    REQUIRE(clipboard.copy("login-test", error));
    CHECK(received == "login-test");
    CHECK(error.empty());
}

TEST_CASE("Le presse-papiers propage une erreur sans modifier le texte", "[clipboard]")
{
    Clipboard clipboard([](std::string_view, std::string& error) {
        error = "backend indisponible";
        return false;
    });
    std::string error;

    CHECK_FALSE(clipboard.copy("secret-test", error));
    CHECK(error == "backend indisponible");
}
