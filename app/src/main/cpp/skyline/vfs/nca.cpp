// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <crypto/aes_cipher.h>
#include <loader/loader.h>

#include "ctr_encrypted_backing.h"
#include "region_backing.h"
#include "partition_filesystem.h"
#include "nca.h"
#include "rom_filesystem.h"
#include "bktr.h"
#include "directory.h"

namespace skyline::vfs {
    using namespace loader;

    NCA::NCA(std::shared_ptr<vfs::Backing> pBacking, std::shared_ptr<crypto::KeyStore> pKeyStore, bool pUseKeyArea)
        : backing(std::move(pBacking)), keyStore(std::move(pKeyStore)), useKeyArea(pUseKeyArea) {
        header = backing->Read<NCAHeader>();

        if (header.magic != util::MakeMagic<u32>("NCA3")) {
            if (!keyStore->headerKey)
                throw loader_exception(LoaderResult::MissingHeaderKey);

            crypto::AesCipher cipher(*keyStore->headerKey, MBEDTLS_CIPHER_AES_128_XTS);
            cipher.XtsDecrypt({reinterpret_cast<u8 *>(&header), sizeof(NCAHeader)}, 0, 0x200);

            // Check if decryption was successful
            if (header.magic != util::MakeMagic<u32>("NCA3"))
                throw loader_exception(LoaderResult::ParsingError);
            encrypted = true;
        }

        contentType = header.contentType;
        rightsIdEmpty = header.rightsId == crypto::KeyStore::Key128{};

        const std::size_t numberSections{static_cast<size_t>(std::ranges::count_if(header.sectionTables, [](const NCASectionTableEntry &entry) {
            return entry.mediaOffset > 0;
        }))};

        sections.resize(numberSections);
        const auto lengthSections{constant::SectionHeaderSize * numberSections};

        if (encrypted) {
            std::vector<u8> raw(lengthSections);

            backing->Read(raw, constant::SectionHeaderOffset);

            crypto::AesCipher cipher(*keyStore->headerKey, MBEDTLS_CIPHER_AES_128_XTS);
            cipher.XtsDecrypt(reinterpret_cast<u8 *>(sections.data()), reinterpret_cast<u8 *>(raw.data()), lengthSections, 2, constant::SectionHeaderSize);
        } else {
            for (size_t i{}; i < lengthSections; i++)
                sections.push_back(backing->Read<NCASectionHeader>());
        }

        for (std::size_t i = 0; i < sections.size(); ++i) {
            const auto &section = sections[i];

            ValidateNCA(section);

            if (section.raw.header.fsType == NcaSectionFsType::RomFs) {
                ReadRomFs(section, header.sectionTables[i]);
            } else if (section.raw.header.fsType == NcaSectionFsType::PFS0) {
                ReadPfs0(section, header.sectionTables[i]);
            }
        }
    }

    NCA::NCA(std::optional<vfs::NCA> updateNca, std::shared_ptr<crypto::KeyStore> pKeyStore, std::shared_ptr<vfs::Backing> bktrBaseRomfs,
             u64 bktrBaseIvfcOffset, bool pUseKeyArea)
        : romFs(updateNca->romFs), header(updateNca->header), sections(std::move(updateNca->sections)), encrypted(updateNca->encrypted), backing(std::move(updateNca->backing)),
        keyStore(std::move(pKeyStore)), bktrBaseRomfs(std::move(bktrBaseRomfs)), bktrBaseIvfcOffset(bktrBaseIvfcOffset), useKeyArea(pUseKeyArea) {

        useKeyArea = false;
        contentType = header.contentType;
        rightsIdEmpty = header.rightsId == crypto::KeyStore::Key128{};

        if (!updateNca)
            throw loader_exception(LoaderResult::ParsingError);

        for (std::size_t i = 0; i < sections.size(); ++i) {
            const auto &section = sections[i];

            ValidateNCA(section);

            if (section.raw.header.fsType == NcaSectionFsType::RomFs)
                ReadRomFs(section, header.sectionTables[i]);
        }
    }

    void NCA::ReadPfs0(const NCASectionHeader &section, const NCASectionTableEntry &entry) {
        size_t offset{static_cast<size_t>(entry.mediaOffset) * constant::MediaUnitSize + section.pfs0.pfs0HeaderOffset};
        size_t size{constant::MediaUnitSize * static_cast<size_t>(entry.mediaEndOffset - entry.mediaOffset)};

        auto pfs{std::make_shared<PartitionFileSystem>(CreateBacking(section, std::make_shared<RegionBacking>(GetSectionBacking(section, entry), offset, size), offset))};

        if (contentType == NCAContentType::Program) {
            // An ExeFS must always contain an NPDM and a main NSO, whereas the logo section will always contain a logo and a startup movie
            if (pfs->FileExists("main") && pfs->FileExists("main.npdm"))
                exeFs = std::move(pfs);
            else if (pfs->FileExists("NintendoLogo.png") && pfs->FileExists("StartupMovie.gif"))
                logo = std::move(pfs);
        } else if (contentType == NCAContentType::Meta) {
            cnmt = std::move(pfs);
        }
    }

    void NCA::ReadRomFs(const NCASectionHeader &sectionHeader, const NCASectionTableEntry &entry) {
        const std::size_t baseOffset{entry.mediaOffset * constant::MediaUnitSize};
        ivfcOffset = sectionHeader.romfs.ivfc.levels[constant::IvfcMaxLevel - 1].offset;
        const std::size_t romFsOffset{baseOffset + ivfcOffset};
        const std::size_t romFsSize{sectionHeader.romfs.ivfc.levels[constant::IvfcMaxLevel - 1].size};
        auto sectionBacking{GetSectionBacking(sectionHeader, entry)};
        auto decryptedBacking{CreateBacking(sectionHeader, std::make_shared<RegionBacking>(sectionBacking, romFsOffset, romFsSize), romFsOffset)};

        if (sectionHeader.raw.header.encryptionType == NcaSectionEncryptionType::BKTR && bktrBaseRomfs && romFs) {
            const u64 size{constant::MediaUnitSize * (entry.mediaEndOffset - entry.mediaOffset)};
            const u64 offset{sectionHeader.romfs.ivfc.levels[constant::IvfcMaxLevel - 1].offset};

            RelocationBlock relocationBlock{romFs->Read<RelocationBlock>(sectionHeader.bktr.relocation.offset - offset)};
            SubsectionBlock subsectionBlock{romFs->Read<SubsectionBlock>(sectionHeader.bktr.subsection.offset - offset)};

            std::vector<RelocationBucketRaw> relocationBucketsRaw((sectionHeader.bktr.relocation.size - sizeof(RelocationBlock)) / sizeof(RelocationBucketRaw));
            auto regionBackingRelocation{std::make_shared<RegionBacking>(romFs, sectionHeader.bktr.relocation.offset + sizeof(RelocationBlock) - offset, sectionHeader.bktr.relocation.size - sizeof(RelocationBlock))};
            regionBackingRelocation->Read<RelocationBucketRaw>(relocationBucketsRaw);

            std::vector<SubsectionBucketRaw> subsectionBucketsRaw((sectionHeader.bktr.subsection.size - sizeof(SubsectionBlock)) / sizeof(SubsectionBucketRaw));
            auto regionBackingSubsection{std::make_shared<RegionBacking>(romFs, sectionHeader.bktr.subsection.offset + sizeof(SubsectionBlock) - offset, sectionHeader.bktr.subsection.size - sizeof(SubsectionBlock))};
            regionBackingSubsection->Read<SubsectionBucketRaw>(subsectionBucketsRaw);

            std::vector<RelocationBucket> relocationBuckets;
            relocationBuckets.reserve(relocationBucketsRaw.size());
            for (const RelocationBucketRaw &rawBucket : relocationBucketsRaw)
                relocationBuckets.push_back(ConvertRelocationBucketRaw(rawBucket));

            std::vector<SubsectionBucket> subsectionBuckets;
            subsectionBuckets.reserve(subsectionBucketsRaw.size());
            for (const SubsectionBucketRaw &rawBucket : subsectionBucketsRaw)
                subsectionBuckets.push_back(ConvertSubsectionBucketRaw(rawBucket));

            u32 ctrLow;
            std::memcpy(&ctrLow, sectionHeader.raw.sectionCtr.data(), sizeof(ctrLow));
            subsectionBuckets.back().entries.push_back({sectionHeader.bktr.relocation.offset, {0}, ctrLow});
            subsectionBuckets.back().entries.push_back({size, {0}, 0});

            auto key{!(rightsIdEmpty || useKeyArea) ? GetTitleKey() : GetKeyAreaKey(sectionHeader.raw.header.encryptionType)};

            auto bktr{std::make_shared<BKTR>(
                bktrBaseRomfs, std::make_shared<RegionBacking>(sectionBacking, baseOffset, romFsSize),
                relocationBlock, relocationBuckets, subsectionBlock, subsectionBuckets, encrypted,
                encrypted ? key : std::array<u8, 0x10>{}, baseOffset, bktrBaseIvfcOffset,
                sectionHeader.raw.sectionCtr)};

            romFs = std::make_shared<RegionBacking>(bktr, sectionHeader.romfs.ivfc.levels[constant::IvfcMaxLevel - 1].offset, romFsSize);
        } else {
            romFs = std::move(decryptedBacking);
        }
    }

    std::shared_ptr<Backing> NCA::CreateBacking(const NCASectionHeader &sectionHeader, std::shared_ptr<Backing> rawBacking, size_t offset) {
        if (!encrypted)
            return rawBacking;

        switch (sectionHeader.raw.header.encryptionType) {
            case NcaSectionEncryptionType::None:
                return rawBacking;
            case NcaSectionEncryptionType::CTR:
            case NcaSectionEncryptionType::BKTR: {
                auto key{!(rightsIdEmpty || useKeyArea) ? GetTitleKey() : GetKeyAreaKey(sectionHeader.raw.header.encryptionType)};

                std::array<u8, 0x10> ctr{};
                for (std::size_t i = 0; i < 8; ++i) {
                    ctr[i] = sectionHeader.raw.sectionCtr[8 - i - 1];
                }

                return std::make_shared<CtrEncryptedBacking>(ctr, key, std::move(rawBacking), offset);
            }
            default:
                return nullptr;
        }
    }

    std::shared_ptr<Backing> NCA::GetSectionBacking(const NCASectionHeader &sectionHeader, const NCASectionTableEntry &entry) {
        if (sectionHeader.raw.sparseInfo.generation != 0)
            return CreateSparseBacking(sectionHeader, entry);
        return backing;
    }

    std::shared_ptr<Backing> NCA::CreateSparseBacking(const NCASectionHeader &sectionHeader, const NCASectionTableEntry &entry) {
        const auto &sparseInfo{sectionHeader.raw.sparseInfo};
        const u64 sectionStart{static_cast<u64>(entry.mediaOffset) * constant::MediaUnitSize};
        const u64 virtualSize{static_cast<u64>(entry.mediaEndOffset - entry.mediaOffset) * constant::MediaUnitSize};

        NCABucketTreeHeader bucketHeader;
        std::memcpy(&bucketHeader, sparseInfo.bucket.tableHeader.data(), sizeof(bucketHeader));

        if (bucketHeader.magic != util::MakeMagic<u32>("BKTR")) {
            LOGW("Sparse section has an invalid bucket tree header: 0x{:X}", bucketHeader.magic);
            throw loader_exception(LoaderResult::ErrorSparseNCA);
        }

        if (bucketHeader.entryCount == 0) {
            // A sparse layer without entries means the entire section reads as zeroes
            RelocationBlock relocationBlock{};
            relocationBlock.numberBuckets = 1;
            relocationBlock.size = virtualSize;

            std::vector<RelocationBucket> relocationBuckets{RelocationBucket{1, virtualSize, {RelocationEntry{0, 0, 1}}}};

            LOGD("Mounted an empty sparse section: virtual size 0x{:X}", virtualSize);
            return std::make_shared<SparseBacking>(nullptr, relocationBlock, std::move(relocationBuckets), sectionStart);
        }

        const u64 tableOffset{sparseInfo.bucket.tableOffset};
        const u64 tableSize{sparseInfo.bucket.tableSize};
        const u64 physicalSize{tableOffset + tableSize};

        if (tableSize < sizeof(RelocationBlock) || (tableSize - sizeof(RelocationBlock)) % sizeof(RelocationBucketRaw) != 0) {
            LOGW("Sparse section has a malformed table: offset 0x{:X}, size 0x{:X}", tableOffset, tableSize);
            throw loader_exception(LoaderResult::ErrorSparseNCA);
        }

        if (sparseInfo.physicalOffset + physicalSize > backing->size) {
            LOGW("Sparse section physical region is out of bounds: 0x{:X}+0x{:X}/0x{:X}", sparseInfo.physicalOffset, physicalSize, backing->size);
            throw loader_exception(LoaderResult::ErrorSparseNCA);
        }

        std::shared_ptr<Backing> tableBacking{std::make_shared<RegionBacking>(backing, sparseInfo.physicalOffset + tableOffset, tableSize)};

        if (encrypted && sectionHeader.raw.header.encryptionType != NcaSectionEncryptionType::None) {
            auto key{!(rightsIdEmpty || useKeyArea) ? GetTitleKey() : GetKeyAreaKey(sectionHeader.raw.header.encryptionType)};

            // The upper half of the IV retains the section's secure value while the generation is replaced with the sparse generation
            std::array<u8, 0x10> ctr{};
            for (std::size_t i = 0; i < 4; ++i)
                ctr[i] = sectionHeader.raw.sectionCtr[8 - i - 1];

            u32 sparseGeneration{static_cast<u32>(sparseInfo.generation) << 16};
            for (std::size_t i = 0; i < 4; ++i)
                ctr[4 + i] = static_cast<u8>(sparseGeneration >> (24 - i * 8));

            tableBacking = std::make_shared<CtrEncryptedBacking>(ctr, key, std::move(tableBacking), sparseInfo.physicalOffset + tableOffset);
        }

        auto relocationBlock{tableBacking->Read<RelocationBlock>()};

        std::vector<RelocationBucketRaw> relocationBucketsRaw((tableSize - sizeof(RelocationBlock)) / sizeof(RelocationBucketRaw));
        auto regionBackingRelocation{std::make_shared<RegionBacking>(tableBacking, sizeof(RelocationBlock), tableSize - sizeof(RelocationBlock))};
        regionBackingRelocation->Read<RelocationBucketRaw>(relocationBucketsRaw);

        if (relocationBlock.numberBuckets == 0 || relocationBlock.numberBuckets > relocationBucketsRaw.size() || relocationBlock.size == 0) {
            LOGW("Sparse section relocation block is malformed: {} buckets, size 0x{:X}", relocationBlock.numberBuckets, relocationBlock.size);
            throw loader_exception(LoaderResult::ErrorSparseNCA);
        }

        std::vector<RelocationBucket> relocationBuckets;
        relocationBuckets.reserve(relocationBlock.numberBuckets);
        for (std::size_t i = 0; i < relocationBlock.numberBuckets; ++i) {
            const RelocationBucketRaw &rawBucket{relocationBucketsRaw[i]};
            if (rawBucket.numberEntries > rawBucket.relocationEntries.size()) {
                LOGW("Sparse section relocation bucket {} is malformed: {} entries", i, rawBucket.numberEntries);
                throw loader_exception(LoaderResult::ErrorSparseNCA);
            }

            relocationBuckets.push_back(ConvertRelocationBucketRaw(rawBucket));
        }

        if (relocationBlock.size > virtualSize)
            LOGW("Sparse section virtual size mismatch: 0x{:X} > 0x{:X}", relocationBlock.size, virtualSize);

        auto physicalBacking{std::make_shared<RegionBacking>(backing, sparseInfo.physicalOffset, physicalSize)};

        LOGD("Mounted a sparse section: virtual offset 0x{:X}, size 0x{:X}, physical offset 0x{:X}, {} entries", sectionStart, relocationBlock.size, sparseInfo.physicalOffset, bucketHeader.entryCount);
        return std::make_shared<SparseBacking>(std::move(physicalBacking), relocationBlock, std::move(relocationBuckets), sectionStart);
    }

    u8 NCA::GetKeyGeneration() {
        u8 legacyGen{static_cast<u8>(header.cryptoType)};
        u8 gen{static_cast<u8>(header.cryptoType2)};
        gen = std::max<u8>(legacyGen, gen);
        return gen > 0 ? gen - 1 : gen;
    }

    crypto::KeyStore::Key128 NCA::GetTitleKey() {
        u8 keyGeneration{GetKeyGeneration()};

        auto titleKey{keyStore->GetTitleKey(header.rightsId)};
        auto &titleKek{keyStore->titleKek[keyGeneration]};

        if (!titleKey)
            throw loader_exception(LoaderResult::MissingTitleKey);
        if (!titleKek)
            throw loader_exception(LoaderResult::MissingTitleKek);

        crypto::AesCipher cipher(*titleKek, MBEDTLS_CIPHER_AES_128_ECB);
        cipher.Decrypt(*titleKey);
        return *titleKey;
    }

    crypto::KeyStore::Key128 NCA::GetKeyAreaKey(NcaSectionEncryptionType type) {
        auto keyArea{[this, &type](crypto::KeyStore::IndexedKeys128 &keys) {
            u8 keyGeneration{GetKeyGeneration()};

            auto &keyArea{keys[keyGeneration]};

            if (!keyArea)
                throw loader_exception(LoaderResult::MissingKeyArea);

            size_t keyAreaIndex;
            switch (type) {
                case NcaSectionEncryptionType::XTS:
                    keyAreaIndex = 0;
                    break;
                case NcaSectionEncryptionType::CTR:
                case NcaSectionEncryptionType::BKTR:
                    keyAreaIndex = 2;
                    break;
                default:
                    throw exception("Unsupported NcaSectionEncryptionType");
            }

            crypto::KeyStore::Key128 decryptedKeyArea;
            crypto::AesCipher cipher(*keyArea, MBEDTLS_CIPHER_AES_128_ECB);
            cipher.Decrypt(decryptedKeyArea.data(), header.keyArea[keyAreaIndex].data(), decryptedKeyArea.size());
            return decryptedKeyArea;
        }};

        switch (header.keyIndex) {
            case NCAKeyAreaEncryptionKeyType::Application:
                return keyArea(keyStore->areaKeyApplication);
            case NCAKeyAreaEncryptionKeyType::Ocean:
                return keyArea(keyStore->areaKeyOcean);
            case NCAKeyAreaEncryptionKeyType::System:
                return keyArea(keyStore->areaKeySystem);
        }
    }

    void NCA::ValidateNCA(const NCASectionHeader &sectionHeader) {
        if (sectionHeader.raw.sparseInfo.generation == 0 &&
            sectionHeader.raw.sparseInfo.bucket.tableOffset != 0 &&
            sectionHeader.raw.sparseInfo.bucket.tableSize != 0) {
            LOGW("NCA section has a sparse table without a generation, cannot mount");
            throw loader_exception(LoaderResult::ErrorSparseNCA);
        }

        if (sectionHeader.raw.compressionInfo.bucket.tableOffset != 0 &&
            sectionHeader.raw.compressionInfo.bucket.tableSize != 0)
            throw loader_exception(LoaderResult::ErrorCompressedNCA);
    }
}
