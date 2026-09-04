/*
 * LocalFileSaver.cpp
 */

#include <iostream>
#include <fstream>
#include <streambuf>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>
#include "FileManager.hpp"
#include "LocalFileSaver.hpp"
#include "LocalFileCommon.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, LocalFileSaver::Error, e )
{
    switch ( e )
    {
        case sgns::LocalFileSaver::Error::READ_ERROR:
            return "File could not be read";
        case sgns::LocalFileSaver::Error::FILE_OPEN_FAIL:
            return "File could not be opened";
    }
    return "Unknown error";
}

namespace sgns
{
    LocalFileSaver *LocalFileSaver::_instance = nullptr;

    void LocalFileSaver::InitializeSingleton()
    {
        if ( _instance == nullptr )
        {
            _instance = new LocalFileSaver();
        }
    }

    LocalFileSaver::LocalFileSaver()
    {
        FileManager::GetInstance().RegisterSaver( "file", this );
    }

    void LocalFileSaver::SaveFile( std::string filename, std::shared_ptr<void> data )
    {
        if ( data == nullptr )
        {
            throw range_error( "Can not save with null data" );
        }
        std::shared_ptr<string> fileContent = std::static_pointer_cast<string>( data );
        ofstream                outputFile( filename, std::ios_base::binary );
        if ( !outputFile.is_open() )
        {
            throw range_error( "Can not create file for save" );
        }
        outputFile.write( fileContent.get()->c_str(), fileContent.get()->size() );
        outputFile.close();
    }

    void LocalFileSaver::SaveASync( std::shared_ptr<boost::asio::io_context>                            ioc,
                              std::function<void( std::shared_ptr<boost::asio::io_context> ioc )> handle_write,
                              std::string                                                         filename,
                              ResultType                                                          data,
                              std::string                                                         suffix,
                              std::shared_ptr<std::string>                                        save_location )
    {
        if ( data.value()->second.data() == nullptr )
        {
            throw range_error( "Can not save with null data" );
        }
        if ( filename.empty() )
        {
            filename = boost::lexical_cast<string>( ( boost::uuids::random_generator() )() ) + "/";
        }

        // Report the save location back to the caller
        if ( save_location )
        {
            *save_location = "file://" + filename;
        }

        //size_t remainingWrites = data.first.size();
        auto remainingWrites = std::make_shared<size_t>( data.value()->first.size() );
        for ( size_t i = 0; i < data.value()->first.size(); ++i )
        {
            //Create Directories for files
            const std::string    &directoryWithFile = filename + data.value()->first[i];
            std::filesystem::path filePath( directoryWithFile );
            std::filesystem::path directory = filePath.parent_path();
            std::filesystem::create_directories( directory );

            //Create Steam for async writes
            std::ofstream file( directoryWithFile, std::ios::binary );
            auto          fileDevice = std::make_shared<LocalFileDevice>( ioc, directoryWithFile, 1 );
            auto          tryopen    = fileDevice->Open();
            if ( tryopen )
            {
                // File open failure
                boost::asio::post( *ioc, [handle_write, ioc]() { handle_write( ioc ); } );
                return;
            }
            async_write( fileDevice->getFile(),
                         boost::asio::buffer( data.value()->second[i].data(), data.value()->second[i].size() ),
                         boost::asio::transfer_exactly( data.value()->second[i].size() ),
                         [fileDevice, ioc, handle_write, data, remainingWrites]( const boost::system::error_code &error,
                                                                                 std::size_t bytes_transferred )
                         {
                             ( *remainingWrites )--;
                             if ( *remainingWrites <= 0 )
                             {
                                 //Handle when written all
                                 handle_write( ioc );
                             }
                         } );
        }
    }

} // End namespace sgns
