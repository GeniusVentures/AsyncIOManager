
#include "FileManager.hpp"
#include "HTTPLoader.hpp"
#include "HTTPCommon.hpp"
#include "HTTPClient.hpp"

#include <filesystem>

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, HTTPLoader::Error, e )
{
    switch ( e )
    {
        case sgns::HTTPLoader::Error::INVALID_URL:
            return "Invalid URL";
    }
    return "Unknown error";
}

namespace sgns
{
    HTTPLoader *HTTPLoader::_instance      = nullptr;
    HTTPLoader *HTTPLoader::_plainInstance = nullptr;

    void HTTPLoader::InitializeSingleton()
    {
        if ( _instance == nullptr )
        {
            _instance = new HTTPLoader( true, 443 );
        }
        FileManager::GetInstance().RegisterLoader( "https", _instance );
        if ( _plainInstance == nullptr )
        {
            _plainInstance = new HTTPLoader( false, 80 );
        }
        FileManager::GetInstance().RegisterLoader( "http", _plainInstance );
    }

    HTTPLoader::HTTPLoader( bool useTLS, unsigned defaultPort )
        : useTLS_( useTLS ), defaultPort_( defaultPort )
    {
    }

    std::shared_ptr<void> HTTPLoader::LoadFile( std::string filename )
    {
        const char *dummyValue = "Inside the HTTPLoader::LoadFile Function";
        // for this test, we don't need to delete the shared_ptr as the data is static, so pass null lambda delete function
        return { (void *)dummyValue, []( void * ) {} };
        /* TODO: scorpioluck20 - Need to implement this. How we load file base on format file?*/
    }

    std::shared_ptr<void> HTTPLoader::LoadASync( std::string                              filename,
                                                 bool                                     parse,
                                                 bool                                     save,
                                                 std::shared_ptr<boost::asio::io_context> ioc,
                                                 CompletionCallback                       handle_read )
    {
        std::shared_ptr<string> result = std::make_shared<string>( "test" );
        //Parse hostname and path
        std::string http_host;
        std::string http_path;
        std::string http_port;
        if ( !parseHTTPUrl( filename, http_host, http_path, http_port ) )
        {
            boost::asio::post( *ioc,
                               [handle_read, ioc]()
                               { handle_read( ioc, outcome::failure( Error::INVALID_URL ), false, false ); } );
            return result;
        }

        // parseHTTPUrl defaults a missing port to "443"; the plain loader
        // must substitute its own default when the URL carried none (D-03)
        if ( !useTLS_ && http_port == "443" )
        {
            http_port = std::to_string( defaultPort_ );
        }

        if ( useTLS_ )
        {
            auto httpDevice = std::make_shared<HTTPDevice>( http_host, http_path, http_port, parse, save );
            httpDevice->StartHTTPDownload( ioc, handle_read );
        }
        else
        {
            // Plain http:// dispatch through the status-aware client, adapted
            // back into the legacy ResultType (filename from path, body only).
            // status intentionally not surfaced on legacy path.
            // Async Execute (never ExecuteBlocking): the caller owns the ioc
            // run-loop (work-guard pattern) — blocking here would double-run
            // or deadlock against the caller's own ioc->run().
            auto                client  = std::make_shared<HTTPClient>( http_host, http_path, http_port );
            http::RequestOptions options;
            options.caCertFile = std::nullopt;
            client->Execute(
                ioc,
                options,
                [client, http_path, parse, save, handle_read, ioc]( std::shared_ptr<boost::asio::io_context>,
                                                                    HTTPClient::Result response )
                {
                    if ( response )
                    {
                        auto finaldata = std::make_shared<
                            std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>();
                        std::filesystem::path p( http_path );
                        finaldata->first.push_back( p.filename().string() );
                        finaldata->second.emplace_back( response.value().body.begin(),
                                                        response.value().body.end() );
                        handle_read( ioc, finaldata, parse, save );
                    }
                    else
                    {
                        handle_read( ioc, outcome::failure( Error::INVALID_URL ), false, false );
                    }
                } );
        }

        return result;
    }

} // End namespace sgns
