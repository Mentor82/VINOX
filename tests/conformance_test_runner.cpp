#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>

#include "vinox/linep.h"
#include "vinox/linep.hpp"

int main() {
    vinox_linep_worker_config cfg;
    vinox_linep_worker_config_init(&cfg);
    cfg.port = 52425;
    cfg.allow_mock_models = 1;

    vinox_linep_worker* worker = nullptr;
    if (vinox_linep_worker_create(&cfg, &worker) != VINOX_STATUS_OK) {
        std::cerr << "Failed to create worker!\n";
        return 1;
    }

    if (vinox_linep_worker_start(worker) != VINOX_STATUS_OK) {
        std::cerr << "Failed to start listener on port " << cfg.port << "\n";
        vinox_linep_worker_destroy(worker);
        return 1;
    }

    std::cout << "VINOX LiNeP V0.2 TCP Socket Listener active on 127.0.0.1:52425. Running conformance test...\n";

    // Run linep-v02-conformance.exe against this active listener
    std::string cmd = "C:\\ai\\LiNeP\\build-v02\\tools\\v0_2\\linep-v02-conformance.exe --endpoint 127.0.0.1:52425 --profile generate";
    int ret = std::system(cmd.c_str());

    vinox_linep_worker_stop(worker);
    vinox_linep_worker_destroy(worker);

    return ret;
}
