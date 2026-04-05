#include "utils/security.h"

#include <array>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace wevoaweb {

namespace {

constexpr std::uint32_t kSha256RoundConstants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr std::uint32_t kSha256InitialState[8] = {
    0x6a09e667,
    0xbb67ae85,
    0x3c6ef372,
    0xa54ff53a,
    0x510e527f,
    0x9b05688c,
    0x1f83d9ab,
    0x5be0cd19,
};

std::uint32_t rotateRight(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

std::vector<std::uint8_t> sha256Bytes(const std::string& input) {
    std::vector<std::uint8_t> message(input.begin(), input.end());
    const std::uint64_t bitLength = static_cast<std::uint64_t>(message.size()) * 8ULL;

    message.push_back(0x80);
    while ((message.size() % 64) != 56) {
        message.push_back(0x00);
    }

    for (int shift = 56; shift >= 0; shift -= 8) {
        message.push_back(static_cast<std::uint8_t>((bitLength >> shift) & 0xFF));
    }

    std::array<std::uint32_t, 8> state {};
    std::copy(std::begin(kSha256InitialState), std::end(kSha256InitialState), state.begin());

    for (std::size_t chunkOffset = 0; chunkOffset < message.size(); chunkOffset += 64) {
        std::array<std::uint32_t, 64> words {};
        for (int index = 0; index < 16; ++index) {
            const std::size_t base = chunkOffset + static_cast<std::size_t>(index) * 4;
            words[index] = (static_cast<std::uint32_t>(message[base]) << 24) |
                           (static_cast<std::uint32_t>(message[base + 1]) << 16) |
                           (static_cast<std::uint32_t>(message[base + 2]) << 8) |
                           static_cast<std::uint32_t>(message[base + 3]);
        }

        for (int index = 16; index < 64; ++index) {
            const std::uint32_t s0 =
                rotateRight(words[index - 15], 7) ^ rotateRight(words[index - 15], 18) ^ (words[index - 15] >> 3);
            const std::uint32_t s1 =
                rotateRight(words[index - 2], 17) ^ rotateRight(words[index - 2], 19) ^ (words[index - 2] >> 10);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }

        std::uint32_t a = state[0];
        std::uint32_t b = state[1];
        std::uint32_t c = state[2];
        std::uint32_t d = state[3];
        std::uint32_t e = state[4];
        std::uint32_t f = state[5];
        std::uint32_t g = state[6];
        std::uint32_t h = state[7];

        for (int index = 0; index < 64; ++index) {
            const std::uint32_t sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            const std::uint32_t choose = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = h + sum1 + choose + kSha256RoundConstants[index] + words[index];
            const std::uint32_t sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = sum0 + majority;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    std::vector<std::uint8_t> digest;
    digest.reserve(32);
    for (const auto word : state) {
        digest.push_back(static_cast<std::uint8_t>((word >> 24) & 0xFF));
        digest.push_back(static_cast<std::uint8_t>((word >> 16) & 0xFF));
        digest.push_back(static_cast<std::uint8_t>((word >> 8) & 0xFF));
        digest.push_back(static_cast<std::uint8_t>(word & 0xFF));
    }

    return digest;
}

std::string toHex(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        stream << std::setw(2) << static_cast<int>(byte);
    }
    return stream.str();
}

std::string bytesToString(const std::vector<std::uint8_t>& bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

bool constantTimeEquals(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }

    unsigned char difference = 0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        difference |= static_cast<unsigned char>(left[index] ^ right[index]);
    }
    return difference == 0;
}

std::vector<std::uint8_t> hmacSha256(const std::string& key, const std::string& message) {
    constexpr std::size_t kBlockSize = 64;

    std::vector<std::uint8_t> keyBlock(kBlockSize, 0);
    std::vector<std::uint8_t> keyBytes(key.begin(), key.end());
    if (keyBytes.size() > kBlockSize) {
        keyBytes = sha256Bytes(key);
    }
    std::copy(keyBytes.begin(), keyBytes.end(), keyBlock.begin());

    std::vector<std::uint8_t> innerPad(kBlockSize);
    std::vector<std::uint8_t> outerPad(kBlockSize);
    for (std::size_t index = 0; index < kBlockSize; ++index) {
        innerPad[index] = static_cast<std::uint8_t>(keyBlock[index] ^ 0x36U);
        outerPad[index] = static_cast<std::uint8_t>(keyBlock[index] ^ 0x5cU);
    }

    const std::vector<std::uint8_t> innerHash = sha256Bytes(bytesToString(innerPad) + message);
    return sha256Bytes(bytesToString(outerPad) + bytesToString(innerHash));
}

std::string legacyHashPasswordWithSalt(const std::string& password,
                                       const std::string& salt,
                                       std::uint32_t iterations) {
    std::string digest = password + ":" + salt;
    for (std::uint32_t iteration = 0; iteration < iterations; ++iteration) {
        digest = toHex(sha256Bytes(digest + ":" + password + ":" + salt));
    }
    return digest;
}

std::string pbkdf2HashPasswordWithSalt(const std::string& password,
                                       const std::string& salt,
                                       std::uint32_t iterations) {
    if (iterations == 0) {
        throw std::runtime_error("PBKDF2 iteration count must be greater than zero.");
    }

    constexpr std::size_t kDerivedKeyLength = 32;
    constexpr std::size_t kDigestLength = 32;

    std::vector<std::uint8_t> derivedKey;
    derivedKey.reserve(kDerivedKeyLength);

    const std::size_t blockCount = (kDerivedKeyLength + kDigestLength - 1) / kDigestLength;
    for (std::uint32_t blockIndex = 1; blockIndex <= blockCount; ++blockIndex) {
        std::string blockSalt = salt;
        blockSalt.push_back(static_cast<char>((blockIndex >> 24) & 0xFF));
        blockSalt.push_back(static_cast<char>((blockIndex >> 16) & 0xFF));
        blockSalt.push_back(static_cast<char>((blockIndex >> 8) & 0xFF));
        blockSalt.push_back(static_cast<char>(blockIndex & 0xFF));

        std::vector<std::uint8_t> u = hmacSha256(password, blockSalt);
        std::vector<std::uint8_t> block = u;
        for (std::uint32_t iteration = 1; iteration < iterations; ++iteration) {
            u = hmacSha256(password, bytesToString(u));
            for (std::size_t index = 0; index < block.size(); ++index) {
                block[index] ^= u[index];
            }
        }

        derivedKey.insert(derivedKey.end(), block.begin(), block.end());
    }

    derivedKey.resize(kDerivedKeyLength);
    return toHex(derivedKey);
}

bool verifyEncodedHash(const std::string& password,
                       const std::string& encodedHash,
                       std::string_view prefix,
                       const std::function<std::string(const std::string&, const std::string&, std::uint32_t)>&
                           deriveDigest) {
    if (encodedHash.rfind(prefix.data(), 0) != 0) {
        return false;
    }

    const auto first = encodedHash.find('$', prefix.size());
    if (first == std::string::npos) {
        return false;
    }
    const auto second = encodedHash.find('$', first + 1);
    if (second == std::string::npos) {
        return false;
    }

    std::uint32_t iterations = 0;
    try {
        iterations = static_cast<std::uint32_t>(std::stoul(encodedHash.substr(prefix.size(), first - prefix.size())));
    } catch (...) {
        return false;
    }

    const std::string salt = encodedHash.substr(first + 1, second - first - 1);
    const std::string expectedDigest = encodedHash.substr(second + 1);
    const std::string actualDigest = deriveDigest(password, salt, iterations);
    return constantTimeEquals(actualDigest, expectedDigest);
}

}  // namespace

std::string sha256Hex(const std::string& value) {
    return toHex(sha256Bytes(value));
}

std::string generateSecureToken(std::size_t byteCount) {
    thread_local std::mt19937_64 generator(std::random_device {}());
    std::uniform_int_distribution<int> distribution(0, 255);

    std::vector<std::uint8_t> bytes;
    bytes.reserve(byteCount);
    for (std::size_t index = 0; index < byteCount; ++index) {
        bytes.push_back(static_cast<std::uint8_t>(distribution(generator)));
    }

    return toHex(bytes);
}

std::string hashPassword(const std::string& password) {
    constexpr std::uint32_t kIterations = 60000;
    const std::string salt = generateSecureToken(16);
    const std::string digest = pbkdf2HashPasswordWithSalt(password, salt, kIterations);
    return "wevoa$pbkdf2-sha256$" + std::to_string(kIterations) + "$" + salt + "$" + digest;
}

bool verifyPassword(const std::string& password, const std::string& encodedHash) {
    constexpr std::string_view kPbkdf2Prefix = "wevoa$pbkdf2-sha256$";
    if (verifyEncodedHash(password, encodedHash, kPbkdf2Prefix, pbkdf2HashPasswordWithSalt)) {
        return true;
    }

    constexpr std::string_view kLegacyPrefix = "wevoa$sha256$";
    return verifyEncodedHash(password, encodedHash, kLegacyPrefix, legacyHashPasswordWithSalt);
}

}  // namespace wevoaweb
