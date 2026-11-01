// main.cpp - Entry point. All real work lives in the modules.
#include "chat_app.h"
#include "config.h"

int main(int argc, char** argv) {
    simplechat::ChatApp app(simplechat::loadConfig(argc, argv));
    return app.run();
}
