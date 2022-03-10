/*
 * Copyright (C) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "auth.h"
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

using namespace Hdc;
#define BIGNUMTOBIT 32

namespace HdcAuth {
// ---------------------------------------Cheat compiler---------------------------------------------------------
#ifdef HDC_HOST

bool AuthVerify(uint8_t *token, uint8_t *sig, int siglen)
{
    return false;
};
bool PostUIConfirm(string publicKey)
{
    return false;
}

#else  // daemon

bool GenerateKey(const char *file)
{
    return false;
};
int AuthSign(void *rsa, const unsigned char *token, size_t tokenSize, void *sig)
{
    return 0;
};
int GetPublicKeyFileBuf(unsigned char *data, size_t len)
{
    return 0;
}

#endif
// ------------------------------------------------------------------------------------------------

const uint32_t RSANUMBYTES = 256;  // 2048 bit key length
const uint32_t RSANUMWORDS = (RSANUMBYTES / sizeof(uint32_t));
struct RSAPublicKey {
    int wordModulusSize;            // Length of n[] in number of uint32_t */
    uint32_t rsaN0inv;              // -1 / n[0] mod 2^32
    uint32_t modulus[RSANUMWORDS];  // modulus as little endian array
    uint32_t rr[RSANUMWORDS];       // R^2 as little endian array
    BN_ULONG exponent;                   // 3 or 65537
};

#ifdef HDC_HOST
// Convert OpenSSL RSA private key to pre-computed RSAPublicKey format
int RSA2RSAPublicKey(RSA *rsa, RSAPublicKey *publicKey)
{
    int result = 1;
    unsigned int i;
    BN_CTX *ctx = BN_CTX_new();
    BIGNUM *r32 = BN_new();
    BIGNUM *rsaRR = BN_new();
    BIGNUM *rsaR = BN_new();
    BIGNUM *rsaRem = BN_new();
    BIGNUM *rsaN0inv = BN_new();
#ifdef OPENSSL_IS_BORINGSSL
    // boringssl
    BIGNUM *n = BN_new();
    BN_copy(n, rsa->n);
    publicKey->exponent = BN_get_word(rsa->e);
#else
    // openssl
#if OPENSSL_VERSION_NUMBER >= 0x10100005L
    BIGNUM *n = (BIGNUM *)RSA_get0_n(rsa);
    publicKey->exponent = BN_get_word(RSA_get0_e(rsa));
#else
    BIGNUM *n = BN_new();
    BN_copy(n, rsa->n);
    publicKey->exponent = BN_get_word(rsa->e);
#endif

#endif  // OPENSSL_IS_BORINGSSL
    while (true) {
        if (RSA_size(rsa) != RSANUMBYTES) {
            result = 0;
            break;
        }

        BN_set_bit(r32, BIGNUMTOBIT);
        BN_set_bit(rsaR, RSANUMWORDS * BIGNUMTOBIT);
        BN_mod_sqr(rsaRR, rsaR, n, ctx);
        BN_div(nullptr, rsaRem, n, r32, ctx);
        BN_mod_inverse(rsaN0inv, rsaRem, r32, ctx);
        publicKey->wordModulusSize = RSANUMWORDS;
        publicKey->rsaN0inv = 0 - BN_get_word(rsaN0inv);
        for (i = 0; i < RSANUMWORDS; ++i) {
            BN_div(rsaRR, rsaRem, rsaRR, r32, ctx);
            publicKey->rr[i] = BN_get_word(rsaRem);
            BN_div(n, rsaRem, n, r32, ctx);
            publicKey->modulus[i] = BN_get_word(rsaRem);
        }
        break;
    }

    BN_free(rsaR);
    BN_free(rsaRR);
    BN_free(n);
    BN_free(r32);
    BN_free(rsaN0inv);
    BN_free(rsaRem);
    BN_CTX_free(ctx);
    return result;
}

int GetUserInfo(char *buf, size_t len)
{
    char hostname[BUF_SIZE_DEFAULT];
    char username[BUF_SIZE_DEFAULT];
    uv_passwd_t pwd;
    int ret = -1;
    size_t bufSize = sizeof(hostname);
    if (uv_os_gethostname(hostname, &bufSize) < 0 && EOK != strcpy_s(hostname, sizeof(hostname), "unknown")) {
        return ERR_API_FAIL;
    }
    if (!uv_os_get_passwd(&pwd) && !strcpy_s(username, sizeof(username), pwd.username)) {
        ret = 0;
    }
    uv_os_free_passwd(&pwd);
    if (ret < 0 && EOK != strcpy_s(username, sizeof(username), "unknown")) {
        return ERR_API_FAIL;
    }
    if (snprintf_s(buf, len, len - 1, " %s@%s", username, hostname) < 0) {
        return ERR_BUF_OVERFLOW;
    }
    return RET_SUCCESS;
}

int WritePublicKeyfile(RSA *private_key, const char *private_key_path)
{
    if (private_key == nullptr || private_key_path == nullptr) {
        return 0;
    }

    RSAPublicKey publicKey;
    char info[BUF_SIZE_DEFAULT];
    int ret = 0;
    string path = private_key_path + string(".pub");

    ret = RSA2RSAPublicKey(private_key, &publicKey);
    if (!ret) {
        WRITE_LOG(LOG_DEBUG, "Failed to convert to publickey\n");
        return 0;
    }
    vector<uint8_t> vec = Base::Base64Encode((const uint8_t *)&publicKey, sizeof(RSAPublicKey));
    if (vec.empty()) {
        return 0;
    }
    GetUserInfo(info, sizeof(info));
    vec.insert(vec.end(), (uint8_t *)info, (uint8_t *)info + strlen(info));
    ret = Base::WriteBinFile(path.c_str(), vec.data(), vec.size(), true);
    return ret >= 0 ? 1 : 0;
}

bool GenerateKey(const char *file)
{
    EVP_PKEY *publicKey = EVP_PKEY_new();
    BIGNUM *exponent = BN_new();
    RSA *rsa = RSA_new();
    mode_t old_mask;
    FILE *fKey = nullptr;
    bool ret = false;

    while (true) {
        WRITE_LOG(LOG_DEBUG, "generate_key '%s'\n", file);
        if (!publicKey || !exponent || !rsa) {
            WRITE_LOG(LOG_DEBUG, "Failed to allocate key");
            break;
        }

        BN_set_word(exponent, RSA_F4);
        RSA_generate_key_ex(rsa, 2048, exponent, nullptr);
        EVP_PKEY_set1_RSA(publicKey, rsa);
        old_mask = umask(077);  // 077:permission

        fKey = fopen(file, "w");
        if (!fKey) {
            WRITE_LOG(LOG_DEBUG, "Failed to open '%s'\n", file);
            umask(old_mask);
            break;
        }
        umask(old_mask);
        if (!PEM_write_PrivateKey(fKey, publicKey, nullptr, nullptr, 0, nullptr, nullptr)) {
            WRITE_LOG(LOG_DEBUG, "Failed to write key");
            break;
        }
        if (!WritePublicKeyfile(rsa, file)) {
            WRITE_LOG(LOG_DEBUG, "Failed to write public key");
            break;
        }
        ret = true;
        break;
    }

    RSA_free(rsa);
    EVP_PKEY_free(publicKey);
    BN_free(exponent);
    if (fKey)
        fclose(fKey);
    return ret;
}

bool ReadKey(const char *file, list<void *> *listPrivateKey)
{
    if (file == nullptr || listPrivateKey == nullptr) {
        return false;
    }

    FILE *f = nullptr;
    bool ret = false;

    while (true) {
        if (!(f = fopen(file, "r"))) {
            break;
        }
        RSA *rsa = RSA_new();
        if (!PEM_read_RSAPrivateKey(f, &rsa, nullptr, nullptr)) {
            RSA_free(rsa);
            break;
        }
        listPrivateKey->push_back((void *)rsa);
        ret = true;
        break;
    }
    if (f) {
        fclose(f);
    }
    return ret;
}

int GetUserKeyPath(string &path)
{
    struct stat status;
    const char harmoneyPath[] = ".harmony";
    const char hdcKeyFile[] = "hdckey";
    char buf[BUF_SIZE_DEFAULT];
    size_t len = BUF_SIZE_DEFAULT;
    // $home
    if (uv_os_homedir(buf, &len) < 0)
        return false;
    string dir = string(buf) + Base::GetPathSep() + string(harmoneyPath) + Base::GetPathSep();
    path = Base::CanonicalizeSpecPath(dir);
    if (stat(path.c_str(), &status)) {
        uv_fs_t req;
        uv_fs_mkdir(nullptr, &req, path.c_str(), 0750, nullptr);  // 0750:permission
        uv_fs_req_cleanup(&req);
        if (req.result < 0) {
            WRITE_LOG(LOG_DEBUG, "Cannot mkdir '%s'", path.c_str());
            return false;
        }
    }
    path += hdcKeyFile;
    return true;
}

bool LoadHostUserKey(list<void *> *listPrivateKey)
{
    if (listPrivateKey == nullptr) {
        return false;
    }

    struct stat status;
    string path;
    if (!GetUserKeyPath(path)) {
        return false;
    }
    if (stat(path.c_str(), &status) == -1) {
        if (!GenerateKey(path.c_str())) {
            WRITE_LOG(LOG_DEBUG, "Failed to generate new key");
            return false;
        }
    }
    if (!ReadKey(path.c_str(), listPrivateKey)) {
        return false;
    }
    return true;
}

int AuthSign(void *rsa, const unsigned char *token, size_t tokenSize, void *sig)
{
    unsigned int len;
    if (!RSA_sign(NID_sha1, token, tokenSize, (unsigned char *)sig, &len, (RSA *)rsa)) {
        return 0;
    }
    return (int)len;
}

int GetPublicKeyFileBuf(unsigned char *data, size_t len)
{
    if (data == nullptr) {
        return 0;
    }
    string path;
    int ret;

    if (!GetUserKeyPath(path)) {
        return 0;
    }
    path += ".pub";
    ret = Base::ReadBinFile(path.c_str(), (void **)data, len);
    if (ret <= 0) {
        return 0;
    }
    data[ret] = '\0';
    return ret + 1;
}

#else  // daemon

bool RSAPublicKey2RSA(const uint8_t *keyBuf, RSA **key)
{
    if (keyBuf == nullptr || key == nullptr) {
        return false;
    }
    const int pubKeyModulusSize = 256;
    const int pubKeyModulusSizeWords = pubKeyModulusSize / 4;

    const RSAPublicKey *keyStruct = reinterpret_cast<const RSAPublicKey *>(keyBuf);
    bool ret = false;
    uint8_t modulusBuffer[pubKeyModulusSize];
    RSA *newKey = RSA_new();
    if (!newKey) {
        goto cleanup;
    }
    if (keyStruct->wordModulusSize != pubKeyModulusSizeWords) {
        goto cleanup;
    }
    if (memcpy_s(modulusBuffer, sizeof(modulusBuffer), keyStruct->modulus, sizeof(modulusBuffer)) != EOK) {
        goto cleanup;
    }
    Base::ReverseBytes(modulusBuffer, sizeof(modulusBuffer));

#ifdef OPENSSL_IS_BORINGSSL
    // boringssl
    newKey->n = BN_bin2bn(modulusBuffer, sizeof(modulusBuffer), nullptr);
    newKey->e = BN_new();
    if (!newKey->e || !BN_set_word(newKey->e, keyStruct->exponent) || !newKey->n) {
        goto cleanup;
    }
#else
    // openssl
#if OPENSSL_VERSION_NUMBER >= 0x10100005L
    RSA_set0_key(newKey, BN_bin2bn(modulusBuffer, sizeof(modulusBuffer), nullptr), BN_new(), BN_new());
#else
    newKey->n = BN_bin2bn(modulusBuffer, sizeof(modulusBuffer), nullptr);
    newKey->e = BN_new();
    if (!newKey->e || !BN_set_word(newKey->e, keyStruct->exponent) || !newKey->n) {
        goto cleanup;
    }
#endif
#endif

    *key = newKey;
    ret = true;

cleanup:
    if (!ret && newKey) {
        RSA_free(newKey);
    }
    return ret;
}

void ReadDaemonKeys(const char *file, list<void *> *listPublicKey)
{
    if (file == nullptr || listPrivateKey == nullptr) {
        return;
    }
    char buf[BUF_SIZE_DEFAULT2] = { 0 };
    char *sep = nullptr;
    int ret;
    FILE *f = fopen(file, "re");
    if (!f) {
        WRITE_LOG(LOG_DEBUG, "Can't open '%s'", file);
        return;
    }
    while (fgets(buf, sizeof(buf), f)) {
        RSAPublicKey *key = new(std::nothrow) RSAPublicKey();
        if (!key) {
            break;
        }
        sep = strpbrk(buf, " \t");
        if (sep) {
            *sep = '\0';
        }
        ret = Base::Base64DecodeBuf(reinterpret_cast<uint8_t *>(buf), strlen(buf), (uint8_t *)key);
        if (ret != sizeof(RSAPublicKey)) {
            WRITE_LOG(LOG_DEBUG, "%s: Invalid base64 data ret=%d", file, ret);
            delete key;
            continue;
        }

        if (key->wordModulusSize != RSANUMWORDS) {
            WRITE_LOG(LOG_DEBUG, "%s: Invalid key len %d\n", file, key->wordModulusSize);
            delete key;
            continue;
        }
        listPublicKey->push_back(key);
    }
    fclose(f);
}

bool AuthVerify(uint8_t *token, uint8_t *sig, int siglen)
{
    list<void *> listPublicKey;
    uint8_t authKeyIndex = 0;
    void *ptr = nullptr;
    int ret = 0;
    int childRet = 0;
    while (KeylistIncrement(&listPublicKey, authKeyIndex, &ptr)) {
        RSA *rsa = nullptr;
        if (!RSAPublicKey2RSA((const uint8_t *)ptr, &rsa)) {
            break;
        }
        childRet = RSA_verify(NID_sha1, (const unsigned char *)token, RSA_TOKEN_SIZE, (const unsigned char *)sig,
                              siglen, rsa);
        RSA_free(rsa);
        if (childRet) {
            ret = 1;
            break;
        }
    }
    FreeKey(true, &listPublicKey);
    return ret;
}

void LoadDaemonKey(list<void *> *listPublicKey)
{
    if (listPublicKey == nullptr) {
        return;
    }
#ifdef HDC_PCDEBUG
    char keyPaths[][BUF_SIZE_SMALL] = { "/root/.harmony/hdckey.pub" };
#else
    char keyPaths[][BUF_SIZE_SMALL] = { "/hdc_keys", "/data/misc/hdc/hdc_keys" };
#endif
    int num = sizeof(keyPaths) / sizeof(keyPaths[0]);
    struct stat buf;

    for (int i = 0; i < num; ++i) {
        char *p = keyPaths[i];
        if (!stat(p, &buf)) {
            WRITE_LOG(LOG_DEBUG, "Loading keys from '%s'", p);
            ReadDaemonKeys(p, listPublicKey);
        }
    }
}

bool PostUIConfirm(string publicKey)
{
    // Because the Hi3516 development board has no UI support for the time being, all public keys are received and
    // By default, the system UI will record the public key /data/misc/hdc/hdckey/data/misc/hdc/hdckey
    return true;
}
#endif  // HDC_HOST

// --------------------------------------common code------------------------------------------
bool KeylistIncrement(list<void *> *listKey, uint8_t &authKeyIndex, void **out)
{
    if (listKey == nullptr || out == nullptr) {
        return false;
    }
    if (listKey->empty()) {
#ifdef HDC_HOST
        LoadHostUserKey(listKey);
#else
        LoadDaemonKey(listKey);
#endif
    }
    if (authKeyIndex == listKey->size()) {
        // all finish
        return false;
    }
    auto listIndex = listKey->begin();
    std::advance(listIndex, ++authKeyIndex);
    *out = *listIndex;
    if (!*out) {
        return false;
    }
    return true;
}

void FreeKey(bool publicOrPrivate, list<void *> *listKey)
{
    if (listKey == nullptr) {
        return;
    }


    for (auto &&v : *listKey) {
        if (v == nullptr) {
            continue;
        }

        if (publicOrPrivate) {
            delete (RSAPublicKey *)v;
            v = nullptr;
        } else {
            RSA_free((RSA *)v);
            v = nullptr;
        }
    }
    listKey->clear();
}
}
