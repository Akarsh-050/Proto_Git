#include <iostream>
#include <string>
#include <curl/curl.h>
#include <vector>
#include <sstream>
#include <iomanip>
#include <fstream>

struct GitPacket {
    int length;
    std::string data;
};

// function to extract the target hash (HEAD) from parsed packets
std::string getTargetHash(const std::vector<GitPacket>& packets) {
    for (const auto& pkt : packets) {
        // Skip the service header and flush packets
        if (pkt.data.empty() || pkt.data[0] == '#') continue;

        // The first real ref line usually contains "HEAD"
        // Format: "SHA-1 name\0capabilities" or "SHA-1 name"
        size_t spacePos = pkt.data.find(' ');
        if (spacePos != std::string::npos) {
            std::string hash = pkt.data.substr(0, spacePos);
            std::string ref = pkt.data.substr(spacePos + 1);

            if (ref.find("HEAD") != std::string::npos) {
                return hash; // Found it!
            }
        }
    }
    return "";
}

// This callback function is called by libcurl as soon as there is data received
// use -> to save response data into a std::string
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t totalSize = size * nmemb;
    userp->append((char*)contents, totalSize);
    return totalSize;
}

// Function to perform HTTP GET request
std::string performGetRequest(const std::string& url) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        
        // Follow redirects (important for some Git hosting providers)
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        // Send the output to our WriteCallback function
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        // Perform the request
        res = curl_easy_perform(curl);

        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }

        curl_easy_cleanup(curl);
    }
    return readBuffer;
}

// helper function to read pkt-line formatted data
std::vector<GitPacket> parsePktLines(const std::string& buffer) {
    std::vector<GitPacket> packets;
    size_t offset = 0;

    while (offset < buffer.length()) {
        // 1. Read the 4-character hex length
        std::string hexLen = buffer.substr(offset, 4);
        
        // 2. Convert hex string to integer
        int len = std::stoi(hexLen, nullptr, 16);
        
        // Handle special "Flush Packet" (0000)
        if (len == 0) {
            packets.push_back({0, ""});
            offset += 4;
            continue;
        }

        // 3. Extract the data 
        std::string data = buffer.substr(offset + 4, len - 4);
        packets.push_back({len, data});

        // 4. Move the offset to the start of the next packet
        offset += len;
    }
    return packets;
}

//Post request to negotiate packfile
std::string negotiatePackfile(const std::string& repoUrl, const std::string& targetHash) {
    CURL* curl = curl_easy_init();
    std::string responseBuffer;

    if(curl) {
        std::string url = repoUrl + "/git-upload-pack";
        
        // Construct the body in pkt-line format
        // "want " (5) + hash (40) + "\n" (1) = 46 chars. 
        // 46 + 4 (hex prefix) = 50 bytes total -> 0032 in hex.
        std::string body = "0032want " + targetHash + "\n00000009done\n";

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.length());

        // Set Headers (Git servers are picky about these)
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/x-git-upload-pack-request");
        headers = curl_slist_append(headers, "Accept: application/x-git-upload-pack-result");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);

        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    return responseBuffer; // This buffer contains the binary PACKFILE
}

// Function to extract packfile data from the POST response
void extractPackfile(const std::string& postResponse, const std::string& outputPath) {
    std::ofstream packFile(outputPath, std::ios::binary);
    size_t offset = 0;

    while (offset < postResponse.length()) {
        // 1. Get packet length
        std::string hexLen = postResponse.substr(offset, 4);
        int len = std::stoi(hexLen, nullptr, 16);

        if (len == 0) {
            offset += 4;
            continue;
        }

        // 2. Check the Channel Byte (the 5th byte)
        unsigned char channel = postResponse[offset + 4];
        
        // 3. If it's Channel 1, it's our Packfile data
        if (channel == 1) {
            // Data starts at offset + 5 (4 bytes len + 1 byte channel)
            // Length includes the 4 bytes header, so data length is len - 5
            packFile.write(&postResponse[offset + 5], len - 5);
        } 
        else if (channel == 2) {
            // Optional: Print progress messages to console
            std::string msg = postResponse.substr(offset + 5, len - 5);
            std::cerr << "Remote: " << msg;
        }
        else{
            // Unknown channel, skip
            std::cerr << "Unknown channel: " << (int)channel << "\n";
        }

        offset += len;
    }
    packFile.close();
}


int main() {

    // http get request 
    std::string URL_RECEIVED = "";
    std::string url = URL_RECEIVED + "/info/refs?service=git-upload-pack";
    std::string rawResponse = performGetRequest(url);
    
    // parsing pkt-line formatted data
    std::vector<GitPacket> packets = parsePktLines(rawResponse);

    // Extracting the Sha-1 of the HEAD reference
    std::string headHash = getTargetHash(packets);

    // Post req for git-upload-pack
    std::string packfileResponse = negotiatePackfile(URL_RECEIVED, headHash);

    //  binary file parsing
    extractPackfile(packfileResponse, "data.pack");

    

return 0;
}