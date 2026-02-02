#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <zlib.h>
#include <vector>
#include <cstring>
#include <openssl/sha.h>
#include <iomanip>
#include <ctime>


// get timestamp in git format
std::string get_git_timestamp() {
    std::time_t now = std::time(nullptr);
    // Hardcoding +0530 for IST (India Standard Time)
    return std::to_string(now) + " +0530"; 
}

// construct commit body
std::string build_commit_content(std::string tree_sha, std::string parent_sha, std::string message) {
    std::stringstream ss;
    
    // 1. Point to the tree
    ss << "tree " << tree_sha << "\n";
    
    // 2. Point to the parent (if it exists)
    if (!parent_sha.empty()) {
        ss << "parent " << parent_sha << "\n";
    }
    
    // 3. Author and Committer info
    std::string timestamp = get_git_timestamp();
    std::string author_info = "Akarsh <akarsh@example.com> " + timestamp;
    
    ss << "author " << author_info << "\n";
    ss << "committer " << author_info << "\n";
    
    // 4. The Message (separated by a blank line)
    ss << "\n" << message << "\n";
    
    return ss.str();
}

// Hashing -> compressing -> storing function
std::string store_git_object(const std::string& content, const std::string& type) {
    // 1. Add Header: "commit <size>\0"
    std::string header = type + " " + std::to_string(content.size()) + '\0';
    std::vector<char> full_data(header.begin(), header.end());
    full_data.insert(full_data.end(), content.begin(), content.end());

    // 2. SHA-1 Hash
    unsigned char hash[20];
    SHA1(reinterpret_cast<const unsigned char*>(full_data.data()), full_data.size(), hash);
    
    std::ostringstream ss;
    for(int i = 0; i < 20; i++) ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    std::string sha = ss.str();

    // 3. Zlib Compress
    uLongf compressed_size = compressBound(full_data.size());
    std::vector<char> compressed(compressed_size);
    compress(reinterpret_cast<Bytef*>(compressed.data()), &compressed_size, 
             reinterpret_cast<const Bytef*>(full_data.data()), full_data.size());
    compressed.resize(compressed_size);

    // 4. Write to Disk
    std::string dir = sha.substr(0, 2);
    std::string file = sha.substr(2);
    std::filesystem::create_directories(".git/objects/" + dir);
    std::ofstream out(".git/objects/" + dir + "/" + file, std::ios::binary);
    out.write(compressed.data(), compressed.size());

    return sha;
}

// tree struct
struct TreeEntry {
    std::string mode;
    std::string name;
    std::vector<unsigned char> hash_bytes; // 20 bytes SHA-1 hash
    // comparator for sorting
    bool operator<(const TreeEntry& other) const {
        return name < other.name;
    }
};

// hex to 20-byte hashing
std::vector<unsigned char> hexToBytes(const std::string& hex) {
    std::vector<unsigned char> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        unsigned char byte = static_cast<unsigned char>(strtol(byteString.c_str(), nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

// function to hash a file as blob and return hex string
std::string hash_file_as_blob(const std::filesystem::path& filePath) {
    // 1. Read file content
    std::ifstream inputFile(filePath, std::ios::binary);
    if (!inputFile.is_open()) {
        throw std::runtime_error("Failed to open file: " + filePath.string());
    }
    std::vector<char> fileData((std::istreambuf_iterator<char>(inputFile)), std::istreambuf_iterator<char>());
    inputFile.close();

    // 2. Create blob object (header + content)
    std::string header = "blob " + std::to_string(fileData.size()) + '\0';
    std::vector<char> objectData(header.begin(), header.end());
    objectData.insert(objectData.end(), fileData.begin(), fileData.end());

    // 3. Compute SHA-1 hash (Binary)
    unsigned char hash[20];
    SHA1(reinterpret_cast<const unsigned char*>(objectData.data()), objectData.size(), hash);

    // 4. Convert binary hash to hex string
    std::ostringstream hashHex;
    for (int i = 0; i < 20; ++i) {
        hashHex << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    std::string hashStr = hashHex.str();

    // 5. Compress data using zlib
    uLongf compressedSize = compressBound(objectData.size());
    std::vector<char> compressedData(compressedSize);
    compress(reinterpret_cast<Bytef*>(compressedData.data()), &compressedSize, 
             reinterpret_cast<const Bytef*>(objectData.data()), objectData.size());
    compressedData.resize(compressedSize);

    // 6. Store object in .git/objects/xx/xxxx...
    std::string dir = hashStr.substr(0, 2);
    std::string file = hashStr.substr(2);
    std::filesystem::path objectDir = std::filesystem::path(".git/objects") / dir;
    std::filesystem::create_directories(objectDir);

    std::ofstream objectFile(objectDir / file, std::ios::binary);
    objectFile.write(compressedData.data(), compressedData.size());
    objectFile.close();

    return hashStr;
}

// recursive write-tree function
std::string write_tree_recursive(std::filesystem::path current_path) {
    std::vector<TreeEntry> entries;

    for (const auto& entry : std::filesystem::directory_iterator(current_path)) {
        std::string name = entry.path().filename().string();
        
        // 1. Skip the .git directory to avoid recursion loops
        if (name == ".git") continue;

        TreeEntry te;
        te.name = name;

        if (entry.is_directory()) {
            te.mode = "40000"; // Mode for directories
            // Recursive call returns the hex hash of the sub-tree
            std::string sub_tree_hash = write_tree_recursive(entry.path());
            te.hash_bytes = hexToBytes(sub_tree_hash);
        } else {
            te.mode = "100644"; // Mode for regular files
            // Use your existing hash-object logic to get file hash
            std::string file_hash = hash_file_as_blob(entry.path()); 
            te.hash_bytes = hexToBytes(file_hash);
        }
        entries.push_back(te);
    }

    // 2. Sort entries alphabetically by name
    std::sort(entries.begin(), entries.end());

    // 3. Construct the binary buffer
    std::vector<char> tree_content;
    for (const auto& e : entries) {
        std::string line = e.mode + " " + e.name + '\0';
        tree_content.insert(tree_content.end(), line.begin(), line.end());
        tree_content.insert(tree_content.end(), e.hash_bytes.begin(), e.hash_bytes.end());
    }

    // 4. Create header, hash, compress, and store (Just like hash-object)
    std::string header = "tree " + std::to_string(tree_content.size()) + '\0';

    // 1. Prepare the final buffer (Header + Content)
    std::vector<char> final_buffer;
    final_buffer.insert(final_buffer.end(), header.begin(), header.end());
    final_buffer.insert(final_buffer.end(), tree_content.begin(), tree_content.end());

    // calculating the sha 20 byte hash 
    unsigned char hash[20];
    SHA1(reinterpret_cast<const unsigned char*>(final_buffer.data()), final_buffer.size(), hash);

    // 3. Convert 20-byte binary to 40-character hex string
    std::ostringstream hashHex;
    for (int i = 0; i < 20; ++i) {
        hashHex << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    std::string final_hex_hash = hashHex.str();

    // 4. Compress the final_buffer using zlib
    uLongf compressedSize = compressBound(final_buffer.size());
    std::vector<char> compressedData(compressedSize);
    if (compress(reinterpret_cast<Bytef*>(compressedData.data()), &compressedSize, 
                reinterpret_cast<const Bytef*>(final_buffer.data()), final_buffer.size()) != Z_OK) {
        throw std::runtime_error("Compression failed for tree object");
    }
    compressedData.resize(compressedSize);

    // 5. Write to disk (.git/objects/xx/xxxx...)
    std::string dir = final_hex_hash.substr(0, 2);
    std::string file = final_hex_hash.substr(2);
    std::filesystem::path objectDir = std::filesystem::path(".git/objects") / dir;
    std::filesystem::create_directories(objectDir);

    std::ofstream objectFile(objectDir / file, std::ios::binary);
    objectFile.write(compressedData.data(), compressedData.size());
    objectFile.close();

    // 6. Return the hex hash so the parent tree can use it..!!
    return final_hex_hash;
}

int main(int argc, char *argv[])
{
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // You can use print statements as follows for debugging, they'll be visible when running tests.
    std::cerr << "Logs from your program will appear here!\n";

    // TODO: Uncomment the code below to pass the first stage
    
    if (argc < 2) {
        std::cerr << "No command provided.\n";
        return EXIT_FAILURE;
    }
    
    std::string command = argv[1];
    
    // handles git init command 
    if (command == "init") {
        try {
            // Create .git directory structure
            std::filesystem::create_directory(".git");
            std::filesystem::create_directory(".git/objects");
            std::filesystem::create_directory(".git/refs");
    
            // Create HEAD file
            std::ofstream headFile(".git/HEAD");
            if (headFile.is_open()) {
                headFile << "ref: refs/heads/main\n";
                headFile.close();
            } else {
                std::cerr << "Failed to create .git/HEAD file.\n";
                return EXIT_FAILURE;
            }
    
            std::cout << "Initialized git directory\n";
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } 

    // handles git cat-file -p <object> command
    else if(command == "cat-file") {
        if(argc < 4 || std::string(argv[2]) != "-p") {
            std::cerr << "Usage: cat-file -p <object>\n";
            std::cerr << "Unknown command " << command <<" "<< argv[2] << '\n';
            return EXIT_FAILURE;
        }
        std::string objectHash = argv[3];

        // Locate object file
        std::string dir = objectHash.substr(0,2);
        std::string file = objectHash.substr(2);
        std::filesystem::path objectPath = std::filesystem::path(".git/objects") / dir / file;

        if(!std::filesystem::exists(objectPath)) {
            std::cerr << "Object " << objectHash << " not found.\n";
            return EXIT_FAILURE;
        }

        // Read compressed object data
        std::ifstream objectFile(objectPath, std::ios::binary);
        std::vector<char> compressedData((std::istreambuf_iterator<char>(objectFile)), std::istreambuf_iterator<char>());


        // Decompress object data using zlib
        std::vector<char> decompressed_data(compressedData.size() * 10); 
        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        inflateInit(&zs);

        zs.next_in = (Bytef*)compressedData.data();
        zs.avail_in = compressedData.size();
        zs.next_out = (Bytef*)decompressed_data.data();
        zs.avail_out = decompressed_data.size();

        inflate(&zs, Z_FINISH);
        inflateEnd(&zs);        

        // Extract and print object content   
        std::string decomp_str(decompressed_data.begin(), decompressed_data.begin() + zs.total_out);
        size_t null_pos = decomp_str.find('\0');
        std::string content = decomp_str.substr(null_pos + 1);
        std::cout << content;

        return EXIT_SUCCESS;
    }

    // handles git hash-object -w <file> command
    else if ( command == "hash-object"){
        if(argc < 4 || std::string(argv[2]) != "-w") {
            std::cerr << "Usage: hash-object -w <file>\n";
            std::cerr << "Unknown command " << command <<" "<< argv[2] << '\n';
            return EXIT_FAILURE;
            }
        
        try {
            std::string hashStr = hash_file_as_blob(argv[3]);
            std::cout << hashStr << '\n';
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
            return EXIT_FAILURE;
        }    
        
    }
    
    // handles git ls-tree --name-only <hash> command
    else if(command == "ls-tree") {
        if(argc < 4 || std::string(argv[2]) != "--name-only") {
            std::cerr << "Usage: ls-tree --name-only <object>\n";
            std::cerr << "Unknown command " << command <<" "<< argv[2] << '\n';
            return EXIT_FAILURE;
        }

        std::string objectHash = argv[3];

        // Locate object file
        std::string dir = objectHash.substr(0,2);
        std::string file = objectHash.substr(2);
        std::filesystem::path objectPath = std::filesystem::path(".git/objects") / dir / file;

        if(!std::filesystem::exists(objectPath)) {
            std::cerr << "Object " << objectHash << " not found.\n";
            return EXIT_FAILURE;
        }

        // Read compressed object data
        std::ifstream objectFile(objectPath, std::ios::binary);
        std::vector<char> compressedData((std::istreambuf_iterator<char>(objectFile)), std::istreambuf_iterator<char>());
    
        // Decompress object data using zlib
        std::vector<char> decompressed_data(compressedData.size() * 10);
        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        inflateInit(&zs);
        zs.next_in = (Bytef*)compressedData.data();
        zs.avail_in = compressedData.size();
        zs.next_out = (Bytef*)decompressed_data.data();
        zs.avail_out = decompressed_data.size();    
        inflate(&zs, Z_FINISH);
        inflateEnd(&zs);

        // Extract and print file names from tree object 
        
        auto it = std::find(decompressed_data.begin(), decompressed_data.begin() + zs.total_out, '\0');
        if (it != decompressed_data.begin() + zs.total_out) {   it++;   }

        while(it<decompressed_data.begin()+ zs.total_out){
            // read mode
            std::string mode;
            while(*it != ' '){
                mode.push_back(*it);
                it++;
            }
            it++; // skip space

            // read filename
            std::string filename;
            while(*it != '\0'){
                filename.push_back(*it);
                it++;
            }
            it++; // skip space

            // read hash 
            std::string hash;
            for(int i=0;i<20;i++){
                char byte = *it;
                hash.push_back(byte);
                it++;
            }
            std::cout << filename << '\n';
        }
    }

    // handles git write-tree command
    else if(command == "write-tree"){
        if(argc != 2) {
            std::cerr << "Usage: write-tree\n";
            std::cerr << "Unknown command " << command <<'\n';
            return EXIT_FAILURE;
        }
        try {
            std::string tree_hash = write_tree_recursive(std::filesystem::current_path());
            std::cout << tree_hash << '\n';
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
            return EXIT_FAILURE;
        }
    }

    // handles git commit-tree <tree-hash> -m <message> command
    else if(command=="commit-tree"){
    
        std::string tree_sha = argv[2];
        std::string parent_sha = "";
        std::string message = "";

        // Basic argument parsing
        for (int i = 3; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "-p" && i + 1 < argc) {parent_sha = argv[++i];} 
            else if (arg == "-m" && i + 1 < argc) {message = argv[++i];}
        }

        // S1 get time setup    
        // construct  commit body
        // hash -> compress -> store

        // construct commit body
        std::string content = build_commit_content(tree_sha, parent_sha, message);
        // hash -> compress -> store
        std::string commit_sha = store_git_object(content, "commit");
    
        std::cout << commit_sha << std::endl;
    }

    else {
        std::cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
