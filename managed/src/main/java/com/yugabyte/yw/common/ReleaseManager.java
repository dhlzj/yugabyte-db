// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import static java.nio.file.StandardCopyOption.REPLACE_EXISTING;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.AddGFlagMetadata;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.ReleaseFormData;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TaskType;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.nio.file.FileSystems;
import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.nio.file.PathMatcher;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.function.Predicate;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import javax.validation.Valid;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FilenameUtils;
import org.apache.commons.lang3.StringUtils;
import play.Configuration;
import play.data.validation.Constraints;
import play.libs.Json;
import play.mvc.Http.Status;

@Slf4j
@Singleton
public class ReleaseManager {

  private static final String DOWNLOAD_HEML_CHART_HTTP_TIMEOUT_PATH =
      "yb.releases.download_helm_chart_http_timeout";
  public static final ConfigHelper.ConfigType CONFIG_TYPE =
      ConfigHelper.ConfigType.SoftwareReleases;
  private static final String YB_PACKAGE_REGEX =
      "yugabyte-(?:ee-)?(.*)-(alma|centos|linux|el8|darwin)(.*).tar.gz";

  private final ConfigHelper configHelper;
  private final Configuration appConfig;
  private final GFlagsValidation gFlagsValidation;
  private final Commissioner commissioner;
  private final AWSUtil awsUtil;
  private final GCPUtil gcpUtil;

  @Inject
  public ReleaseManager(
      ConfigHelper configHelper,
      Configuration appConfig,
      GFlagsValidation gFlagsValidation,
      Commissioner commissioner,
      AWSUtil awsUtil,
      GCPUtil gcpUtil) {
    this.configHelper = configHelper;
    this.appConfig = appConfig;
    this.gFlagsValidation = gFlagsValidation;
    this.commissioner = commissioner;
    this.awsUtil = awsUtil;
    this.gcpUtil = gcpUtil;
  }

  public enum ReleaseState {
    ACTIVE,
    DISABLED,
    DELETED
  }

  @ApiModel(description = "Yugabyte release metadata")
  public static class ReleaseMetadata {

    @ApiModelProperty(value = "Release state", example = "ACTIVE")
    public ReleaseState state = ReleaseState.ACTIVE;

    @ApiModelProperty(value = "Release notes")
    public List<String> notes;

    // File path where the release binary is stored
    @ApiModelProperty(value = "Release file path")
    public String filePath;

    // File path where the release helm chart is stored
    @ApiModelProperty(value = "Helm chart path")
    public String chartPath;

    @ApiModelProperty(value = "Release packages")
    public List<Package> packages;

    // Docker image tag corresponding to the release
    @ApiModelProperty(value = "Release image tag")
    public String imageTag;

    // S3 link and credentials for remote downloading of the release
    @ApiModelProperty(value = "S3 link and credentials")
    public S3Location s3;

    // GCS link and credentials for remote downloading of the release
    @ApiModelProperty(value = "GCS link and credentials")
    public GCSLocation gcs;

    // HTTP link for remote downloading of the release
    @ApiModelProperty(value = "HTTP link to the release")
    public HttpLocation http;

    @lombok.Getter
    @lombok.Setter
    public static class PackagePaths {
      @ApiModelProperty(value = "Path to x86_64 package")
      @Constraints.Pattern(
          message =
              "File path must be prefixed with s3://, gs://, or http(s)://,"
                  + " and must contain path to package file, instead of a directory",
          value = "\\b(?:(https|s3|gs):\\/\\/).+\\b")
      public String x86_64;

      @ApiModelProperty(value = "Checksum for x86_64 package")
      @JsonAlias("x86_64_checksum")
      public String x86_64Checksum;

      @ApiModelProperty(value = "Path to the Helm chart package")
      @Constraints.Pattern(
          message =
              "File path must be prefixed with s3://, gs://, or http(s)://,"
                  + " and must contain path to package file, instead of a directory",
          value = "\\b(?:(https|s3|gs):\\/\\/).+\\b")
      public String helmChart;

      @ApiModelProperty(value = "Checksum for the Helm chart package")
      public String helmChartChecksum;

      //      @ApiModelProperty(value = "Path to aarch64 package")
      //      @Constraints.Pattern(
      //          message = "Must be prefixed with s3:// or http(s)://",
      //          value = "\\b(?:(https|s3|gs):\\/\\/).+\\b")
      //      public String aarch64;
      //
      //      @ApiModelProperty(value = "Checksum for aarch64 package")
      //      @JsonAlias("aarch64_checksum")
      //      public String aarch64Checksum;
    }

    @lombok.Getter
    @lombok.Setter
    public static class S3Location {
      @ApiModelProperty(value = "package paths")
      @Constraints.Required
      @Valid
      public PackagePaths paths;

      // S3 credentials.
      @ApiModelProperty(value = "access key id", hidden = true)
      @Constraints.Required
      public String accessKeyId;

      @ApiModelProperty(value = "access key secret", hidden = true)
      @Constraints.Required
      public String secretAccessKey;
    }

    @lombok.Getter
    @lombok.Setter
    public static class GCSLocation {
      @ApiModelProperty(value = "package paths")
      @Constraints.Required
      @Valid
      public PackagePaths paths;

      // GCS credentials.
      @ApiModelProperty(value = "gcs service key json", hidden = true)
      @Constraints.Required
      public String credentialsJson;
    }

    @lombok.Getter
    @lombok.Setter
    public static class HttpLocation {
      @ApiModelProperty(value = "package paths")
      @Valid
      public PackagePaths paths;
    }

    public static class Package {
      @ApiModelProperty @Constraints.Required public String path;

      @ApiModelProperty @Constraints.Required public Architecture arch;
    }

    public static ReleaseMetadata fromLegacy(String version, Object metadata) {
      // Legacy release metadata would have name and release path alone
      // convert those to new format.
      ReleaseMetadata rm = create(version);
      rm.filePath = (String) metadata;
      rm.chartPath = "";
      return rm;
    }

    public static ReleaseMetadata create(String version) {
      ReleaseMetadata rm = new ReleaseMetadata();
      rm.state = ReleaseState.ACTIVE;
      rm.imageTag = version;
      rm.notes = new ArrayList<>();
      rm.packages = new ArrayList<>();
      return rm;
    }

    public ReleaseMetadata withFilePath(String filePath) {
      this.filePath = filePath;
      return this;
    }

    public ReleaseMetadata withChartPath(String chartPath) {
      this.chartPath = chartPath;
      return this;
    }

    public ReleaseMetadata withPackage(String path, Architecture arch) {
      Package p = new Package();
      p.path = path;
      p.arch = arch;
      this.packages.add(p);
      return this;
    }

    public String toString() {
      return Json.toJson(CommonUtils.maskObject(this)).toString();
    }

    private List<Package> matchPackages(Architecture arch) {
      // Old style release without packages. No matching packages.
      if (packages == null) {
        return Collections.emptyList();
      }
      return packages.stream().filter(p -> p.arch == arch).collect(Collectors.toList());
    }

    public String getFilePath(Region region) {
      Architecture arch = region.getArchitecture();
      // Must be old style region or release with no architecture (or packages).
      if (arch == null || packages == null || packages.isEmpty()) {
        return filePath;
      }
      List<Package> matched = matchPackages(arch);
      if (matched.size() == 0) {
        throw new RuntimeException(
            "Could not find matching package with architecture " + arch.name());
      } else if (matched.size() > 1) {
        log.warn(
            "Found more than one package with matching architecture, picking {}.",
            matched.get(0).path);
      }
      return matched.get(0).path;
    }

    public Boolean matchesRegion(Region region) {
      Architecture arch = region.getArchitecture();
      // Must be old style region or release with no architecture (or packages).
      if (arch == null || packages == null || packages.isEmpty()) {
        return true;
      }
      List<Package> matched = matchPackages(arch);
      return matched.size() > 0;
    }
  }

  private Predicate<Path> getPackageFilter(String pathMatchGlob) {
    return p -> Files.isRegularFile(p) && getPathMatcher(pathMatchGlob).matches(p);
  }

  private PathMatcher getPathMatcher(String pathMatchGlob) {
    return FileSystems.getDefault().getPathMatcher(pathMatchGlob);
  }

  final Predicate<Path> ybChartFilter = getPackageFilter("glob:**yugabyte-*-helm.tar.gz");

  // This regex needs to support old style packages with -ee as well as new style packages without.
  // There are previously existing YW deployments that will have the old packages and users will
  // need to still be able to use said universes and their existing YB releases.
  private static final Pattern ybPackagePattern = Pattern.compile(YB_PACKAGE_REGEX);

  private static final Pattern ybHelmChartPattern = Pattern.compile("yugabyte-(.*).tgz");

  static final Pattern ybVersionPattern =
      Pattern.compile("(.*)(\\d+.\\d+.\\d+(.\\d+)?)(-(b(\\d+)|(\\w+)))?(.*)");

  private static final Pattern ybcPackagePattern =
      Pattern.compile("[^.]+ybc-(?:ee-)?(.*)-(linux|el8)(.*).tar.gz");

  public Map<String, String> getReleaseFiles(
      String releasesPath, Predicate<Path> fileFilter, boolean ybcRelease) {
    Map<String, String> fileMap = new HashMap<>();
    Set<String> duplicateKeys = new HashSet<>();
    try {
      Files.walk(Paths.get(releasesPath))
          .filter(fileFilter)
          .forEach(
              p -> {
                // In case of ybc release, we want to store osType, archType in version key.
                String key =
                    ybcRelease
                        ? StringUtils.removeEnd(
                            p.getName(p.getNameCount() - 1).toString(), ".tar.gz")
                        : p.getName(p.getNameCount() - 2).toString();
                String value = p.toAbsolutePath().toString();
                if (!fileMap.containsKey(key)) {
                  fileMap.put(key, value);
                } else if (!duplicateKeys.contains(key)) {
                  log.warn(
                      String.format(
                          "Skipping %s - it contains multiple releases of same architecture type",
                          key));
                  duplicateKeys.add(key);
                }
              });
      duplicateKeys.forEach(k -> fileMap.remove(k));
    } catch (IOException e) {
      log.error(e.getMessage());
    }
    return fileMap;
  }

  private void updateLocalReleases(
      Map<String, ReleaseMetadata> localReleases,
      Map<String, String> releaseFiles,
      Map<String, String> releaseCharts,
      Architecture arch) {
    releaseFiles.forEach(
        (version, filePath) -> {
          ReleaseMetadata r = localReleases.get(version);
          if (r == null) {
            r =
                ReleaseMetadata.create(version)
                    .withFilePath(filePath)
                    .withChartPath(releaseCharts.getOrDefault(version, ""));
          }
          localReleases.put(version, r.withPackage(filePath, arch));
        });
  }

  public Map<String, ReleaseMetadata> getLocalReleases(String releasesPath) {
    Map<String, String> releaseFiles;
    Map<String, String> releaseCharts = getReleaseFiles(releasesPath, ybChartFilter, false);
    Map<String, ReleaseMetadata> localReleases = new HashMap<>();
    for (Architecture arch : Architecture.values()) {
      releaseFiles = getReleaseFiles(releasesPath, getPackageFilter(arch.getDBGlob()), false);
      updateLocalReleases(localReleases, releaseFiles, releaseCharts, arch);
    }
    return localReleases;
  }

  public Map<String, ReleaseMetadata> getLocalYbcReleases(String releasesPath) {
    Map<String, String> releaseFiles;
    Map<String, ReleaseMetadata> localReleases = new HashMap<>();
    for (Architecture arch : Architecture.values()) {
      releaseFiles = getReleaseFiles(releasesPath, getPackageFilter(arch.getYbcGlob()), true);
      updateLocalReleases(localReleases, releaseFiles, new HashMap<>(), arch);
    }
    return localReleases;
  }

  public Map<String, Object> getReleaseMetadata() {
    return getReleaseMetadata(CONFIG_TYPE);
  }

  public Map<String, Object> getReleaseMetadata(ConfigHelper.ConfigType configType) {
    Map<String, Object> releases = configHelper.getConfig(configType);
    if (releases == null || releases.isEmpty()) {
      log.debug("getReleaseMetadata: No releases found");
      return new HashMap<>();
    }
    releases.forEach(
        (version, metadata) -> {
          if (metadata instanceof String) {
            releases.put(version, ReleaseMetadata.fromLegacy(version, metadata));
          }
        });

    return releases;
  }

  /**
   * It enforces the following two conditions: 1. Proper formatting of the .tar.gz package name. 2.
   * Proper formatting of the DB version in the .tar.gz package name. It also checks and prints a
   * warning for equality of the DB version in the .tar.gz package name with the DB version
   * specified in the Version field.
   *
   * @param version The YBDB version that the package belongs to
   * @param packageName The name to check
   */
  public static void verifyPackageNameFormat(String version, String packageName) {
    Matcher ybPackagePatternMatcher = ybPackagePattern.matcher(packageName);
    Matcher versionPatternMatcher = ybVersionPattern.matcher(packageName);
    if (!ybPackagePatternMatcher.find()) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "The package name of the .tar.gz file is improperly formatted. Please"
              + " check to make sure that you have typed in the package name correctly.");
    }
    if (!versionPatternMatcher.find()) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST,
          "The version of DB in your package name is improperly formatted. Please"
              + " check to make sure that you have typed in the DB version correctly"
              + " in the package name.");
    }
    if (!packageName.contains(version)) {
      log.warn("Package {} might not belong to version {}", packageName, version);
    }
  }

  public static void verifyPackageNameFormat(String version, ReleaseMetadata.PackagePaths paths) {
    if (paths.x86_64 != null) {
      verifyPackageNameFormat(version, FilenameUtils.getName(paths.x86_64));
    }
    //    if (paths.aarch64 != null) {
    //      verifyPackageNameFormat(version, FilenameUtils.getName(paths.aarch64));
    //    }
    if (paths.helmChart != null) {
      Matcher helmChartPatternMatcher = ybHelmChartPattern.matcher(paths.helmChart);
      if (!helmChartPatternMatcher.find()) {
        throw new PlatformServiceException(
            Status.BAD_REQUEST,
            "The package name of the helm chart is improperly formatted. Please"
                + " check to make sure that you have typed in the package name correctly.");
      }
    }
  }

  public static void verifyReleaseFormDataList(List<ReleaseFormData> releaseFormDataList) {
    releaseFormDataList.forEach(
        versionData -> {
          if (versionData.version == null) {
            throw new PlatformServiceException(Status.BAD_REQUEST, "Version is not specified");
          }
          // At least one link should be specified for each version.
          if (versionData.s3 == null && versionData.gcs == null && versionData.http == null) {
            throw new RuntimeException(
                "At least one of S3 link, GCS link, or HTTP link must be specified");
          }

          if (versionData.s3 != null) {
            verifyPackageNameFormat(versionData.version, versionData.s3.paths);
          }
          if (versionData.gcs != null) {
            verifyPackageNameFormat(versionData.version, versionData.gcs.paths);
          }
          if (versionData.http != null) {
            verifyPackageNameFormat(versionData.version, versionData.http.paths);
          }
        });
  }

  public static Map<String, ReleaseMetadata> formDataToReleaseMetadata(
      List<ReleaseFormData> versionDataList) {
    verifyReleaseFormDataList(versionDataList);

    Map<String, ReleaseMetadata> releases = new HashMap<>();
    for (ReleaseFormData versionData : versionDataList) {
      ReleaseMetadata metadata = ReleaseMetadata.create(versionData.version);

      if (versionData.s3 != null) {
        metadata.s3 = versionData.s3;
        metadata.filePath = metadata.s3.paths.x86_64;
      }

      if (versionData.gcs != null) {
        metadata.gcs = versionData.gcs;
        metadata.filePath = metadata.gcs.paths.x86_64;
      }

      if (versionData.http != null) {
        metadata.http = versionData.http;
        metadata.filePath = metadata.http.paths.x86_64;
      }
      releases.put(versionData.version, metadata);
    }
    return releases;
  }

  public void downloadYbHelmChart(String version, ReleaseMetadata metadata) {
    try {
      Path chartPath =
          Paths.get(
              appConfig.getString("yb.releases.path"),
              version,
              String.format("yugabyte-%s-helm.tar.gz", version));
      String checksum = null;
      // Helm chart can be downloaded only from one path.
      if (metadata.s3 != null && metadata.s3.paths.helmChart != null) {
        CustomerConfigStorageS3Data s3ConfigData = new CustomerConfigStorageS3Data();
        s3ConfigData.awsAccessKeyId = metadata.s3.accessKeyId;
        s3ConfigData.awsSecretAccessKey = metadata.s3.secretAccessKey;
        awsUtil.downloadCloudFile(s3ConfigData, metadata.s3.paths.helmChart, chartPath);
        checksum = metadata.s3.paths.helmChartChecksum;
      } else if (metadata.gcs != null && metadata.gcs.paths.helmChart != null) {
        CustomerConfigStorageGCSData gcsConfigData = new CustomerConfigStorageGCSData();
        gcsConfigData.gcsCredentialsJson = metadata.gcs.credentialsJson;
        gcpUtil.downloadCloudFile(gcsConfigData, metadata.gcs.paths.helmChart, chartPath);
        checksum = metadata.gcs.paths.helmChartChecksum;
      } else if (metadata.http != null && metadata.http.paths.helmChart != null) {
        int timeoutMs =
            this.appConfig.getMilliseconds(DOWNLOAD_HEML_CHART_HTTP_TIMEOUT_PATH).intValue();
        org.apache.commons.io.FileUtils.copyURLToFile(
            new URL(metadata.http.paths.helmChart), chartPath.toFile(), timeoutMs, timeoutMs);
        checksum = metadata.http.paths.helmChartChecksum;
      } else {
        chartPath = null;
      }
      // Verify checksum.
      if (chartPath != null && !StringUtils.isBlank(checksum)) {
        checksum = checksum.toLowerCase();
        String[] checksumParts = checksum.split(":", 2);
        if (checksumParts.length < 2) {
          throw new PlatformServiceException(
              Status.BAD_REQUEST,
              String.format(
                  "Checksum must have a format of `[checksum algorithem]:[checksum value]`."
                      + " Got `%s`",
                  checksum));
        }
        String checksumAlgorithm = checksumParts[0];
        String checksumValue = checksumParts[1];
        String computedChecksum = Util.computeFileChecksum(chartPath, checksumAlgorithm);
        if (!checksumValue.equals(computedChecksum)) {
          throw new PlatformServiceException(
              Status.BAD_REQUEST,
              String.format(
                  "Computed checksum of file %s with algorithm %s is `%s` but user input"
                      + " checksum is `%s`",
                  chartPath, checksumAlgorithm, computedChecksum, checksumValue));
        }
      }
      metadata.chartPath = Objects.toString(chartPath);
    } catch (Exception e) {
      throw new RuntimeException(
          String.format(
              "Could not download the helm charts for version %s : %s", version, e.getMessage()));
    }
  }

  public synchronized void addReleaseWithMetadata(String version, ReleaseMetadata metadata) {
    Map<String, Object> currentReleases = getReleaseMetadata();
    if (currentReleases.containsKey(version)) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, String.format("Release already exists for version %s", version));
    }
    log.info("Adding release version {} with metadata {}", version, metadata.toString());
    downloadYbHelmChart(version, metadata);
    currentReleases.put(version, metadata);
    configHelper.loadConfigToDB(ConfigHelper.ConfigType.SoftwareReleases, currentReleases);
  }

  public synchronized void removeRelease(String version) {
    Map<String, Object> currentReleases = getReleaseMetadata();
    String ybReleasesPath = appConfig.getString("yb.releases.path");
    if (currentReleases.containsKey(version)) {
      log.info("Removing release version {}", version);
      currentReleases.remove(version);
      configHelper.loadConfigToDB(ConfigHelper.ConfigType.SoftwareReleases, currentReleases);
    }

    // delete specific release's directory recursively.
    File releaseDirectory = new File(ybReleasesPath, version);
    FileUtils.deleteDirectory(releaseDirectory);
  }

  public synchronized void importLocalReleases() {
    String ybReleasesPath = appConfig.getString("yb.releases.path");
    String ybReleasePath = appConfig.getString("yb.docker.release");
    String ybHelmChartPath = appConfig.getString("yb.helm.packagePath");
    if (ybReleasesPath != null && !ybReleasesPath.isEmpty()) {
      Map<String, Object> currentReleases = getReleaseMetadata();

      // Local copy pattern to account for the presence of characters prior to the file name itself.
      // (ensures that all local releases get imported prior to version checking).
      Pattern ybPackagePatternCopy = Pattern.compile("[^.]+" + YB_PACKAGE_REGEX);

      Pattern ybHelmChartPatternCopy = Pattern.compile("[^.]+yugabyte-(.*)-helm.tar.gz");

      copyFiles(ybReleasePath, ybReleasesPath, ybPackagePatternCopy, currentReleases.keySet());
      copyFiles(ybHelmChartPath, ybReleasesPath, ybHelmChartPatternCopy, currentReleases.keySet());
      Map<String, ReleaseMetadata> localReleases = getLocalReleases(ybReleasesPath);
      localReleases.keySet().removeAll(currentReleases.keySet());
      log.debug("Current releases: [ {} ]", currentReleases.keySet().toString());
      log.debug("Local releases: [ {} ]", localReleases.keySet());

      // As described in the diff, we don't touch the currrent releases that have already been
      // imported. We
      // perform the same checks that we did in the import dialog case here prior to the import).

      // If there is an error for one release, there is still a possibility that the user has
      // imported multiple
      // releases locally, and that the other ones have been named properly. We err on the cautious
      // side, and
      // immediately throw a Runtime Exception. The user will be able to import local releases only
      // if all of
      // them are properly formatted, and none otherwise.

      if (!localReleases.isEmpty()) {

        Pattern ybPackagePatternRequiredInChartPath =
            Pattern.compile("(.*)yugabyte-(?:ee-)?(.*)-(helm)(.*).tar.gz");

        Pattern ybVersionPatternRequired =
            Pattern.compile("^(\\d+.\\d+.\\d+(.\\d+)?)(-(b(\\d+)|(\\w+)))?$");

        for (String version : localReleases.keySet()) {

          String associatedFilePath = localReleases.get(version).filePath;

          String associatedChartPath = localReleases.get(version).chartPath;

          String filePackageName =
              associatedFilePath.split("/")[(associatedFilePath.split("/")).length - 1];

          String chartPackageName =
              associatedChartPath.split("/")[(associatedChartPath.split("/")).length - 1];

          Matcher versionPatternMatcher = ybVersionPatternRequired.matcher(version);

          Matcher packagePatternFileMatcher = ybPackagePattern.matcher(filePackageName);

          Matcher packagePatternChartMatcher =
              ybPackagePatternRequiredInChartPath.matcher(chartPackageName);

          Matcher versionPatternMatcherInPackageNameFilePath =
              ybVersionPattern.matcher(filePackageName);

          Matcher versionPatternMatcherInPackageNameChartPath =
              ybVersionPattern.matcher(chartPackageName);

          if (!versionPatternMatcher.find()) {

            throw new RuntimeException(
                "The version name in the folder of the imported local release is improperly "
                    + "formatted. Please check to make sure that the folder with the version name "
                    + "is named correctly.");
          }

          if (!versionPatternMatcherInPackageNameFilePath.find()) {

            throw new RuntimeException(
                "In the file path, the version of DB in your package name in the imported "
                    + "local release is improperly formatted. Please "
                    + " check to make sure that you have named the .tar.gz file with "
                    + " the appropriate DB version.");
          }

          if (!filePackageName.contains(version)) {

            throw new RuntimeException(
                "The version of DB that you have specified in the folder name in the "
                    + "imported local release does not match the version of DB in the "
                    + "package name in the imported local release (specifed through the "
                    + "file path). Please make sure that you have named the directory and "
                    + ".tar.gz file appropriately so that the DB version in the package "
                    + "name matches the DB version in the folder name.");
          }

          if (!associatedChartPath.equals("")) {

            if (!versionPatternMatcherInPackageNameChartPath.find()) {

              throw new RuntimeException(
                  "In the chart path, the version of DB in your package name in the imported "
                      + "local release is improperly formatted. Please "
                      + " check to make sure that you have named the .tar.gz file with "
                      + " the appropriate DB version.");
            }

            if (!chartPackageName.contains(version)) {

              throw new RuntimeException(
                  "The version of DB that you have specified in the folder name in the "
                      + "imported local release does not match the version of DB in the "
                      + "package name in the imported local release (specifed through the "
                      + "chart path). Please make sure that you have named the directory and "
                      + ".tar.gz file appropriately so that the DB version in the package "
                      + "name matches the DB version in the folder name.");
            }
          }
          // Add gFlag metadata for newly added release.
          addGFlagsMetadataFiles(version, localReleases.get(version));
        }
        log.info("Importing local releases: [ {} ]", Json.toJson(localReleases));
        localReleases.forEach(currentReleases::put);
        configHelper.loadConfigToDB(ConfigHelper.ConfigType.SoftwareReleases, currentReleases);
      }
    }

    log.info("Starting ybc local releases");
    String ybcReleasesPath = appConfig.getString("ybc.releases.path");
    String ybcReleasePath = appConfig.getString("ybc.docker.release");
    log.info("ybcReleasesPath: " + ybcReleasesPath);
    log.info("ybcReleasePath: " + ybcReleasePath);
    if (ybcReleasesPath != null && !ybcReleasesPath.isEmpty()) {
      Map<String, Object> currentYbcReleases =
          getReleaseMetadata(ConfigHelper.ConfigType.YbcSoftwareReleases);
      File ybcReleasePathFile = new File(ybcReleasePath);
      File ybcReleasesPathFile = new File(ybcReleasesPath);
      if (ybcReleasePathFile.exists() && ybcReleasesPathFile.exists()) {
        // No need to skip copying packages as we want to allow multiple arch type of a ybc-version.
        copyFiles(ybcReleasePath, ybcReleasesPath, ybcPackagePattern, null);
        Map<String, ReleaseMetadata> localYbcReleases = getLocalYbcReleases(ybcReleasesPath);
        localYbcReleases.keySet().removeAll(currentYbcReleases.keySet());
        log.info("Current ybc releases: [ {} ]", currentYbcReleases.keySet().toString());
        log.info("Local ybc releases: [ {} ]", localYbcReleases.keySet().toString());
        if (!localYbcReleases.isEmpty()) {
          log.info("Importing local releases: [ {} ]", Json.toJson(localYbcReleases));
          currentYbcReleases.putAll(localYbcReleases);
          configHelper.loadConfigToDB(
              ConfigHelper.ConfigType.YbcSoftwareReleases, currentYbcReleases);
        }
      } else {
        log.warn(
            "ybc release dir: {} and/or ybc releases dir: {} not present",
            ybcReleasePath,
            ybcReleasesPath);
      }
    }
  }

  /** Idempotent method to update all releases with packages if possible. */
  public synchronized void updateCurrentReleases() {
    Map<String, Object> currentReleases = getReleaseMetadata();
    Map<String, Object> updatedReleases = new HashMap<>();
    currentReleases.forEach(
        (version, object) -> {
          ReleaseMetadata rm = metadataFromObject(object);
          // update packages if possible
          if ((rm.packages == null || rm.packages.isEmpty())
              && !(rm.filePath == null || rm.filePath.isEmpty())) {
            Path fp = null;
            try {
              fp = Paths.get(rm.filePath);
            } catch (InvalidPathException e) {
              log.error("Error {} getting package path for version {}", e.getMessage(), version);
            }
            if (fp != null) {
              for (Architecture arch : Architecture.values()) {
                if (getPathMatcher(arch.getDBGlob()).matches(fp)) {
                  rm.packages = (rm.packages == null) ? new ArrayList<>() : rm.packages;
                  rm = rm.withPackage(rm.filePath, arch);
                }
              }
            }
            if (rm.packages == null || rm.packages.isEmpty()) {
              log.warn(
                  "Could not match any available architectures to existing release {}", version);
            }
          }
          updatedReleases.put(version, rm);
        });
    configHelper.loadConfigToDB(ConfigHelper.ConfigType.SoftwareReleases, updatedReleases);
  }

  public void addGFlagsMetadataFiles(String version, ReleaseMetadata releaseMetadata) {
    try {
      List<String> missingGFlagsFilesList = gFlagsValidation.getMissingGFlagFileList(version);
      if (missingGFlagsFilesList.size() != 0) {
        String releasesPath = appConfig.getString(Util.YB_RELEASES_PATH);
        if (isLocalRelease(releaseMetadata)) {
          try (InputStream inputStream = getTarGZipDBPackageInputStream(version, releaseMetadata)) {
            gFlagsValidation.fetchGFlagFilesFromTarGZipInputStream(
                inputStream, version, missingGFlagsFilesList, releasesPath);
          }
          log.info("Successfully added gFlags metadata for version: {}", version);
        } else {
          AddGFlagMetadata.Params taskParams = new AddGFlagMetadata.Params();
          taskParams.version = version;
          taskParams.releaseMetadata = releaseMetadata;
          taskParams.requiredGFlagsFileList = missingGFlagsFilesList;
          taskParams.releasesPath = releasesPath;
          commissioner.submit(TaskType.AddGFlagMetadata, taskParams);
        }
      } else {
        log.warn("Skipping gFlags metadata addition as all files are already present");
      }
    } catch (Exception e) {
      log.error("Could not add GFlags metadata as it errored out with: {}", e.getMessage());
    }
  }

  public synchronized InputStream getTarGZipDBPackageInputStream(
      String version, ReleaseMetadata releaseMetadata) throws Exception {
    if (releaseMetadata.s3 != null) {
      CustomerConfigStorageS3Data configData = new CustomerConfigStorageS3Data();
      configData.awsAccessKeyId = releaseMetadata.s3.getAccessKeyId();
      configData.awsSecretAccessKey = releaseMetadata.s3.secretAccessKey;
      return CloudUtil.getCloudUtil(Util.S3)
          .getCloudFileInputStream(configData, releaseMetadata.s3.paths.getX86_64());
    } else if (releaseMetadata.gcs != null) {
      CustomerConfigStorageGCSData configData = new CustomerConfigStorageGCSData();
      configData.gcsCredentialsJson = releaseMetadata.gcs.credentialsJson;
      return CloudUtil.getCloudUtil(Util.GCS)
          .getCloudFileInputStream(configData, releaseMetadata.gcs.paths.getX86_64());
    } else if (releaseMetadata.http != null) {
      return new URL(releaseMetadata.http.getPaths().getX86_64()).openStream();
    } else {
      if (!Files.exists(Paths.get(releaseMetadata.filePath))) {
        throw new RuntimeException(
            "Cannot add gFlags metadata for version: "
                + version
                + " as no file was present at location: "
                + releaseMetadata.filePath);
      }
      return new FileInputStream(releaseMetadata.filePath);
    }
  }

  public synchronized void updateReleaseMetadata(String version, ReleaseMetadata newData) {
    Map<String, Object> currentReleases = getReleaseMetadata();
    if (currentReleases.containsKey(version)) {
      currentReleases.put(version, newData);
      configHelper.loadConfigToDB(ConfigHelper.ConfigType.SoftwareReleases, currentReleases);
    }
  }

  /**
   * This method copies release files that match a specific regex to a destination directory.
   *
   * @param sourceDir (str): Source directory to move files from
   * @param destinationDir (str): Destination directory to move files to
   * @param fileRegex (str): Regular expression specifying files to move
   * @param skipVersions : Set of versions to ignore while copying. version is the first matching
   *     group from fileRegex
   */
  private static void copyFiles(
      String sourceDir, String destinationDir, Pattern fileRegex, Set<String> skipVersions) {
    if (sourceDir == null || sourceDir.isEmpty()) {
      return;
    }

    try {
      Files.walk(Paths.get(sourceDir))
          .map(String::valueOf)
          .map(fileRegex::matcher)
          .filter(Matcher::matches)
          .forEach(
              match -> {
                String version = match.group(1);
                if (skipVersions != null && skipVersions.contains(version)) {
                  log.debug("Skipping re-copy of release files for {}", version);
                  return;
                }
                File releaseFile = new File(match.group());
                File destinationFolder = new File(destinationDir, version);
                File destinationFile = new File(destinationFolder, releaseFile.getName());
                if (!destinationFolder.exists()) {
                  destinationFolder.mkdir();
                }
                try {
                  Files.copy(releaseFile.toPath(), destinationFile.toPath(), REPLACE_EXISTING);
                } catch (IOException e) {
                  throw new RuntimeException(
                      "Unable to copy release file "
                          + releaseFile.toPath()
                          + " to "
                          + destinationFile);
                }
              });
    } catch (IOException e) {
      log.error(e.getMessage());
      throw new RuntimeException("Unable to look up release files in " + sourceDir);
    }
  }

  public ReleaseMetadata getReleaseByVersion(String version) {
    Object metadata = getReleaseMetadata().get(version);
    if (metadata == null) {
      return null;
    }
    return metadataFromObject(metadata);
  }

  public ReleaseMetadata getYbcReleaseByVersion(String version, String osType, String archType) {
    version = String.format("ybc-%s-%s-%s", version, osType, archType);
    Object metadata = getReleaseMetadata(ConfigHelper.ConfigType.YbcSoftwareReleases).get(version);
    if (metadata == null) {
      return null;
    }
    return metadataFromObject(metadata);
  }

  public ReleaseMetadata metadataFromObject(Object object) {
    return Json.fromJson(Json.toJson(object), ReleaseMetadata.class);
  }

  public Map<String, String> getReleases() {
    return (Map) configHelper.getConfig(CONFIG_TYPE);
  }

  public boolean getInUse(String version) {
    return Universe.existsRelease(version);
  }

  private boolean isLocalRelease(ReleaseMetadata rm) {
    return !(rm.s3 != null || rm.gcs != null || rm.http != null);
  }
}
