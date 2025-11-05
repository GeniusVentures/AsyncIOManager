// IPFSSaver.cpp

#include <iostream>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <boost/asio.hpp>
#include "FileManager.hpp"
#include "IPFSSaver.hpp"
#include "libp2p/multi/content_identifier_codec.hpp"

namespace sgns
{
    IPFSSaver* IPFSSaver::_instance = nullptr;
    void IPFSSaver::InitializeSingleton() {
        if (_instance == nullptr) {
            _instance = new IPFSSaver();
        }
    }

    IPFSSaver::IPFSSaver() {
        FileManager::GetInstance().RegisterSaver("ipfs", this);
    }

    void IPFSSaver::setBitswap(std::shared_ptr<sgns::ipfs_bitswap::Bitswap> bitswap)
    {
        externalBitswap_ = bitswap;
        m_logger->info("External bitswap instance set for IPFS saver");
    }

    bool IPFSSaver::hasExternalBitswap() const
    {
        return externalBitswap_ != nullptr;
    }

    void IPFSSaver::SaveFile(std::string filename, std::shared_ptr<void> data) {
        m_logger->info("Inside the IPFSSaver::SaveFile Function");
    }

    void IPFSSaver::SaveASync(std::shared_ptr<boost::asio::io_context> ioc, 
                              std::function<void(std::shared_ptr<boost::asio::io_context> ioc)> handle_write, 
                              std::string filename, 
                              ResultType data, 
                              std::string suffix) 
    {
        m_logger->info("Publishing content to IPFS via bitswap for: {}", filename);
        
        if (!data.has_value() || !data.value() || data.value()->first.empty()) {
            m_logger->error("Cannot save with null or empty data");
            boost::asio::post(*ioc, [handle_write, ioc]() {
                handle_write(ioc);
            });
            return;
        }

        if (!externalBitswap_) {
            m_logger->error("No bitswap instance set - cannot publish to IPFS. Call setBitswap() first.");
            boost::asio::post(*ioc, [handle_write, ioc]() {
                handle_write(ioc);
            });
            return;
        }

        auto& filePaths = data.value()->first;
        auto& fileContents = data.value()->second;

        if (filePaths.size() != fileContents.size()) {
            m_logger->error("Mismatch between file paths ({}) and contents ({})", filePaths.size(), fileContents.size());
            boost::asio::post(*ioc, [handle_write, ioc]() {
                handle_write(ioc);
            });
            return;
        }

        if (filePaths.size() == 1) {
            // Single file publishing
            auto tempPath = std::filesystem::temp_directory_path() / ("ipfs_temp_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::string tempFilePath = tempPath.string();
            
            try {
                // Write content to temporary file
                std::ofstream tempFile(tempFilePath, std::ios::binary);
                if (!tempFile.is_open()) {
                    m_logger->error("Failed to create temporary file: {}", tempFilePath);
                    boost::asio::post(*ioc, [handle_write, ioc]() {
                        handle_write(ioc);
                    });
                    return;
                }
                
                tempFile.write(fileContents[0].data(), fileContents[0].size());
                tempFile.close();
                
                m_logger->info("Publishing single file: {} (size: {} bytes)", filePaths[0], fileContents[0].size());
                
                externalBitswap_->PublishFile(tempFilePath, 
                    [=](libp2p::outcome::result<sgns::ipfs_bitswap::CID> result) {
                        // Clean up temporary file
                        std::filesystem::remove(tempFilePath);
                        
                        if (result.has_value()) {
                            auto cidString = libp2p::multi::ContentIdentifierCodec::toString(result.value());
                            if (cidString.has_value()) {
                                m_logger->info("Successfully published file '{}' to IPFS with CID: {}", 
                                             filePaths[0], cidString.value());
                            } else {
                                m_logger->info("Successfully published file '{}' to IPFS", filePaths[0]);
                            }
                        } else {
                            m_logger->error("Failed to publish file '{}': {}", filePaths[0], result.error().message());
                        }
                        
                        boost::asio::post(*ioc, [handle_write, ioc]() {
                            handle_write(ioc);
                        });
                    }
                );
                
            } catch (const std::exception& e) {
                m_logger->error("Exception during single file publishing: {}", e.what());
                std::filesystem::remove(tempFilePath);
                boost::asio::post(*ioc, [handle_write, ioc]() {
                    handle_write(ioc);
                });
            }
            
        } else {
            // Multi-file publishing (create temporary directory structure)
            auto tempDir = std::filesystem::temp_directory_path() / ("ipfs_temp_dir_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::string tempDirPath = tempDir.string();
            
            try {
                std::filesystem::create_directories(tempDirPath);
                
                // Write all files to temporary directory
                for (size_t i = 0; i < filePaths.size(); ++i) {
                    std::filesystem::path fullPath = std::filesystem::path(tempDirPath) / filePaths[i];
                    std::filesystem::create_directories(fullPath.parent_path());
                    
                    std::ofstream tempFile(fullPath, std::ios::binary);
                    if (!tempFile.is_open()) {
                        m_logger->error("Failed to create temporary file: {}", fullPath.string());
                        continue;
                    }
                    
                    tempFile.write(fileContents[i].data(), fileContents[i].size());
                    tempFile.close();
                }
                
                m_logger->info("Publishing directory with {} files", filePaths.size());
                
                externalBitswap_->PublishDirectory(tempDirPath,
                    [=](libp2p::outcome::result<sgns::ipfs_bitswap::CID> result) {
                        // Clean up temporary directory
                        std::filesystem::remove_all(tempDirPath);
                        
                        if (result.has_value()) {
                            auto cidString = libp2p::multi::ContentIdentifierCodec::toString(result.value());
                            if (cidString.has_value()) {
                                m_logger->info("Successfully published directory to IPFS with root CID: {}", cidString.value());
                            } else {
                                m_logger->info("Successfully published directory to IPFS");
                            }
                        } else {
                            m_logger->error("Failed to publish directory: {}", result.error().message());
                        }
                        
                        boost::asio::post(*ioc, [handle_write, ioc]() {
                            handle_write(ioc);
                        });
                    }
                );
                
            } catch (const std::exception& e) {
                m_logger->error("Exception during directory publishing: {}", e.what());
                std::filesystem::remove_all(tempDirPath);
                boost::asio::post(*ioc, [handle_write, ioc]() {
                    handle_write(ioc);
                });
            }
        }
    }
}

