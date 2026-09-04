// FileExample.cpp — minimal file:// load → save roundtrip through FileManager.
// Usage: FileExample [input-url] [output-dir-url]
//   defaults: input  file://example_data.bin   (relative to the process CWD — run from example/)
//             output file://example_output/    (roundtrip lands in example_output/<basename>)
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include "FileManager.hpp"

int main( int argc, char **argv )
{
    const std::string inputUrl = argc > 1 ? argv[1] : "file://example_data.bin";
    const std::string saveUrl  = argc > 2 ? argv[2] : "file://example_output/";

    FileManager::GetInstance().InitializeSingletons();

    auto ioc = std::make_shared<boost::asio::io_context>();

    // ---- Phase 1: load --------------------------------------------------
    // NOTE: FileManager::ResultType (outcome::result<T>) has a deleted default
    // constructor, so the result is captured via std::optional — assigned only on
    // success; failure leaves it empty and reports through the callback.
    std::optional<FileManager::ResultType> loaded;
    FileManager::GetInstance().LoadASync( inputUrl,
                                          false,
                                          ioc,
                                          [&]( FileManager::ResultType result )
                                          {
                                              if ( result )
                                              {
                                                  const auto &data = result.value();
                                                  std::cout << "Loaded \"" << data->first[0] << "\" ("
                                                            << data->second[0].size() << " bytes) from " << inputUrl
                                                            << std::endl;
                                                  loaded = result;
                                              }
                                              else
                                              {
                                                  std::cout << "Load failed: " << result.error().message()
                                                            << std::endl;
                                              }
                                          },
                                          "" );
    ioc->run(); // returns when FileManager's outstanding-operations counter drains (stop())

    if ( !loaded.has_value() )
    {
        return 1;
    }

    // ---- Phase 2: save (restart clears the stopped flag — see D4) --------
    std::optional<FileManager::ResultType> saved;
    FileManager::GetInstance().SaveASync( saveUrl,
                                          loaded.value(),
                                          ioc,
                                          [&]( FileManager::ResultType result )
                                          {
                                              if ( result )
                                              {
                                                  const auto &data = result.value();
                                                  std::cout << "Saved \"" << data->first[0] << "\" ("
                                                            << data->second[0].size() << " bytes) to " << saveUrl
                                                            << std::endl;
                                                  saved = result;
                                              }
                                              else
                                              {
                                                  std::cout << "Save failed: " << result.error().message()
                                                            << std::endl;
                                              }
                                          } );
    ioc->restart();
    ioc->run();

    return saved.has_value() ? 0 : 1;
}
