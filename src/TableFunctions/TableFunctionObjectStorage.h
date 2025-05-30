#pragma once

#include <Disks/ObjectStorages/IObjectStorage_fwd.h>
#include <Formats/FormatFactory.h>
#include <Storages/ObjectStorage/DataLakes/DataLakeConfiguration.h>
#include <Storages/ObjectStorage/StorageObjectStorage.h>
#include <Storages/ObjectStorage/StorageObjectStorageSettings.h>
#include <Storages/VirtualColumnUtils.h>
#include <TableFunctions/ITableFunction.h>

#include "config.h"


namespace DB
{

class Context;
class StorageS3Configuration;
class StorageAzureConfiguration;
class StorageHDFSConfiguration;
class StorageLocalConfiguration;
struct S3StorageSettings;
struct AzureStorageSettings;
struct HDFSStorageSettings;

struct AzureDefinition
{
    static constexpr auto name = "azureBlobStorage";
    static constexpr auto storage_type_name = "Azure";
};

struct S3Definition
{
    static constexpr auto name = "s3";
    static constexpr auto storage_type_name = "S3";
};

struct GCSDefinition
{
    static constexpr auto name = "gcs";
    static constexpr auto storage_type_name = "GCS";
};

struct COSNDefinition
{
    static constexpr auto name = "cosn";
    static constexpr auto storage_type_name = "COSN";
};

struct OSSDefinition
{
    static constexpr auto name = "oss";
    static constexpr auto storage_type_name = "OSS";
};

struct HDFSDefinition
{
    static constexpr auto name = "hdfs";
    static constexpr auto storage_type_name = "HDFS";
};

struct LocalDefinition
{
    static constexpr auto name = "local";
    static constexpr auto storage_type_name = "Local";
};

struct IcebergDefinition
{
    static constexpr auto name = "iceberg";
    static constexpr auto storage_type_name = "S3";
};

struct IcebergS3Definition
{
    static constexpr auto name = "icebergS3";
    static constexpr auto storage_type_name = "S3";
};

struct IcebergAzureDefinition
{
    static constexpr auto name = "icebergAzure";
    static constexpr auto storage_type_name = "Azure";
};

struct IcebergLocalDefinition
{
    static constexpr auto name = "icebergLocal";
    static constexpr auto storage_type_name = "Local";
};

struct IcebergHDFSDefinition
{
    static constexpr auto name = "icebergHDFS";
    static constexpr auto storage_type_name = "HDFS";
};

struct DeltaLakeDefinition
{
    static constexpr auto name = "deltaLake";
    static constexpr auto storage_type_name = "S3";
};

struct DeltaLakeS3Definition
{
    static constexpr auto name = "deltaLakeS3";
    static constexpr auto storage_type_name = "S3";
};

struct DeltaLakeAzureDefinition
{
    static constexpr auto name = "deltaLakeAzure";
    static constexpr auto storage_type_name = "Azure";
};

struct DeltaLakeLocalDefinition // New definition for local Delta Lake
{
    static constexpr auto name = "deltaLakeLocal";
    static constexpr auto storage_type_name = "Local";
};

struct HudiDefinition
{
    static constexpr auto name = "hudi";
    static constexpr auto storage_type_name = "S3";
};

template <typename Definition, typename Configuration>
class TableFunctionObjectStorage : public ITableFunction
{
public:
    static constexpr auto name = Definition::name;

    String getName() const override { return name; }

    bool hasStaticStructure() const override { return configuration->structure != "auto"; }

    bool needStructureHint() const override { return configuration->structure == "auto"; }

    void setStructureHint(const ColumnsDescription & structure_hint_) override { structure_hint = structure_hint_; }

    bool supportsReadingSubsetOfColumns(const ContextPtr & context) override
    {
        return configuration->format != "auto" && FormatFactory::instance().checkIfFormatSupportsSubsetOfColumns(configuration->format, context);
    }

    std::unordered_set<String> getVirtualsToCheckBeforeUsingStructureHint() const override
    {
        return VirtualColumnUtils::getVirtualNamesForFileLikeStorage();
    }

    virtual void parseArgumentsImpl(ASTs & args, const ContextPtr & context)
    {
        StorageObjectStorage::Configuration::initialize(*getConfiguration(), args, context, true, settings);
    }

    static void updateStructureAndFormatArgumentsIfNeeded(
      ASTs & args,
      const String & structure,
      const String & format,
      const ContextPtr & context)
    {
        Configuration().addStructureAndFormatToArgsIfNeeded(args, structure, format, context, /*with_structure=*/true);
    }

protected:
    using ConfigurationPtr = StorageObjectStorage::ConfigurationPtr;

    StoragePtr executeImpl(
        const ASTPtr & ast_function,
        ContextPtr context,
        const std::string & table_name,
        ColumnsDescription cached_columns,
        bool is_insert_query) const override;

    const char * getStorageTypeName() const override { return Definition::storage_type_name; }

    ColumnsDescription getActualTableStructure(ContextPtr context, bool is_insert_query) const override;
    void parseArguments(const ASTPtr & ast_function, ContextPtr context) override;

    ObjectStoragePtr getObjectStorage(const ContextPtr & context, bool create_readonly) const;
    ConfigurationPtr getConfiguration() const;

    mutable ConfigurationPtr configuration;
    mutable ObjectStoragePtr object_storage;
    ColumnsDescription structure_hint;
    std::shared_ptr<StorageObjectStorageSettings> settings;

    std::vector<size_t> skipAnalysisForArguments(const QueryTreeNodePtr & query_node_table_function, ContextPtr context) const override;
};

#if USE_AWS_S3
using TableFunctionS3 = TableFunctionObjectStorage<S3Definition, StorageS3Configuration>;
#endif

#if USE_AZURE_BLOB_STORAGE
using TableFunctionAzureBlob = TableFunctionObjectStorage<AzureDefinition, StorageAzureConfiguration>;
#endif

#if USE_HDFS
using TableFunctionHDFS = TableFunctionObjectStorage<HDFSDefinition, StorageHDFSConfiguration>;
#endif

using TableFunctionLocal = TableFunctionObjectStorage<LocalDefinition, StorageLocalConfiguration>;


#if USE_AVRO
#    if USE_AWS_S3
using TableFunctionIceberg = TableFunctionObjectStorage<IcebergDefinition, StorageS3IcebergConfiguration>;
using TableFunctionIcebergS3 = TableFunctionObjectStorage<IcebergS3Definition, StorageS3IcebergConfiguration>;
#    endif
#    if USE_AZURE_BLOB_STORAGE
using TableFunctionIcebergAzure = TableFunctionObjectStorage<IcebergAzureDefinition, StorageAzureIcebergConfiguration>;
#    endif
#    if USE_HDFS
using TableFunctionIcebergHDFS = TableFunctionObjectStorage<IcebergHDFSDefinition, StorageHDFSIcebergConfiguration>;
#    endif
using TableFunctionIcebergLocal = TableFunctionObjectStorage<IcebergLocalDefinition, StorageLocalIcebergConfiguration>;
#endif
#if USE_PARQUET && USE_DELTA_KERNEL_RS
#if USE_AWS_S3
using TableFunctionDeltaLake = TableFunctionObjectStorage<DeltaLakeDefinition, StorageS3DeltaLakeConfiguration>;
using TableFunctionDeltaLakeS3 = TableFunctionObjectStorage<DeltaLakeS3Definition, StorageS3DeltaLakeConfiguration>;
#endif
#if USE_AZURE_BLOB_STORAGE
using TableFunctionDeltaLakeAzure = TableFunctionObjectStorage<DeltaLakeAzureDefinition, StorageAzureDeltaLakeConfiguration>;
#endif
// New alias for local Delta Lake table function
using TableFunctionDeltaLakeLocal = TableFunctionObjectStorage<DeltaLakeLocalDefinition, StorageLocalDeltaLakeConfiguration>;
#endif
#if USE_AWS_S3
using TableFunctionHudi = TableFunctionObjectStorage<HudiDefinition, StorageS3HudiConfiguration>;
#endif
}
