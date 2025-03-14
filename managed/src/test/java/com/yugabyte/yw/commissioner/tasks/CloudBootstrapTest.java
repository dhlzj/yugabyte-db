// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.base.Strings;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.AccessManager.KeyType;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.AccessKey.KeyInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class CloudBootstrapTest extends CommissionerBaseTest {

  private static final String HOST_VPC_REGION = "host-vpc-region";
  private static final String HOST_VPC_ID = "host-vpc-id";
  private static final String DEST_VPC_ID = "dest-vpc-id";

  private void mockRegionMetadata(Common.CloudType cloudType) {
    Map<String, Object> regionMetadata = new HashMap<>();
    regionMetadata.put("name", "Mock Region");
    regionMetadata.put("latitude", 36.778261);
    regionMetadata.put("longitude", -119.417932);

    when(mockConfigHelper.getRegionMetadata(cloudType))
        .thenReturn(
            ImmutableMap.of(
                // AWS regions to use.
                "us-west-1", regionMetadata,
                "us-west-2", regionMetadata,
                "us-east-1", regionMetadata,
                // GCP regions to use.
                "us-west1", regionMetadata,
                "us-east1", regionMetadata));
  }

  private UUID submitTask(CloudBootstrap.Params taskParams) {
    return commissioner.submit(TaskType.CloudBootstrap, taskParams);
  }

  private CloudBootstrap.Params getBaseTaskParams() {
    CloudBootstrap.Params taskParams = new CloudBootstrap.Params();
    taskParams.providerUUID = defaultProvider.uuid;
    taskParams.hostVpcRegion = HOST_VPC_REGION;
    taskParams.hostVpcId = HOST_VPC_ID;
    taskParams.destVpcId = DEST_VPC_ID;
    taskParams.sshPort = 12345;
    taskParams.airGapInstall = false;
    taskParams.skipKeyPairValidate = false;
    return taskParams;
  }

  private void validateCloudBootstrapSuccess(
      CloudBootstrap.Params taskParams,
      JsonNode zoneInfo,
      List<String> expectedRegions,
      String expectedProviderCode,
      boolean customAccessKey,
      boolean customAzMapping,
      boolean customSecurityGroup,
      boolean customImageId)
      throws InterruptedException {
    validateCloudBootstrapSuccess(
        taskParams,
        zoneInfo,
        expectedRegions,
        expectedProviderCode,
        customAccessKey,
        customAzMapping,
        customSecurityGroup,
        customImageId,
        false);
  }

  private void validateCloudBootstrapSuccess(
      CloudBootstrap.Params taskParams,
      JsonNode zoneInfo,
      List<String> expectedRegions,
      String expectedProviderCode,
      boolean customAccessKey,
      boolean customAzMapping,
      boolean customSecurityGroup,
      boolean customImageId,
      boolean hasSecondarySubnet)
      throws InterruptedException {
    validateCloudBootstrapSuccess(
        taskParams,
        zoneInfo,
        expectedRegions,
        expectedProviderCode,
        customAccessKey,
        customAzMapping,
        customSecurityGroup,
        customImageId,
        hasSecondarySubnet,
        false);
  }

  private void validateCloudBootstrapSuccess(
      CloudBootstrap.Params taskParams,
      JsonNode zoneInfo,
      List<String> expectedRegions,
      String expectedProviderCode,
      boolean customAccessKey,
      boolean customAzMapping,
      boolean customSecurityGroup,
      boolean customImageId,
      boolean hasSecondarySubnet,
      boolean isAddRegion)
      throws InterruptedException {
    Provider provider = Provider.get(taskParams.providerUUID);
    // Mock region metadata.
    mockRegionMetadata(Common.CloudType.valueOf(provider.code));
    // TODO(bogdan): we don't really care about the output now..
    when(mockNetworkManager.bootstrap(any(), any(), anyString())).thenReturn(Json.parse("{}"));
    when(mockCloudQueryHelper.getZones(any(UUID.class), anyString())).thenReturn(zoneInfo);
    when(mockCloudQueryHelper.getZones(any(UUID.class), anyString(), anyString()))
        .thenReturn(zoneInfo);
    String defaultImage = "test_image_id";
    when(mockCloudQueryHelper.getDefaultImage(any(Region.class))).thenReturn(defaultImage);
    String x86_64 = "x86_64";
    when(mockCloudQueryHelper.getImageArchitecture(any(Region.class))).thenReturn(x86_64);
    taskParams.providerUUID = provider.uuid;

    UUID taskUUID = submitTask(taskParams);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(Success, taskInfo.getTaskState());
    if (expectedProviderCode.equals("aws")) {
      verify(mockAWSInitializer, times(isAddRegion ? 2 : 1))
          .initialize(defaultCustomer.uuid, provider.uuid);
    } else if (expectedProviderCode.equals("gcp")) {
      verify(mockGCPInitializer, times(isAddRegion ? 2 : 1))
          .initialize(defaultCustomer.uuid, provider.uuid);
    } else {
      fail("Only support AWS and GCP for now.");
    }
    // TODO(bogdan): do we want a different handling here?
    String customPayload = Json.stringify(Json.toJson(taskParams));
    verify(mockNetworkManager, times(isAddRegion ? 0 : 1))
        .bootstrap(null, provider.uuid, customPayload);
    assertEquals(taskParams.perRegionMetadata.size(), expectedRegions.size());
    // Check per-region settings.
    for (Map.Entry<String, CloudBootstrap.Params.PerRegionMetadata> entry :
        taskParams.perRegionMetadata.entrySet()) {
      String regionName = entry.getKey();
      CloudBootstrap.Params.PerRegionMetadata metadata = entry.getValue();
      // Expected region.
      assertTrue(expectedRegions.contains(regionName));
      Region r = Region.getByCode(provider, regionName);
      assertNotNull(r);
      // Check AccessKey info.
      if (customAccessKey) {
        // TODO: might need to add port here.
        verify(mockAccessManager, times(1))
            .saveAndAddKey(
                eq(r.uuid),
                eq(taskParams.sshPrivateKeyContent),
                eq(taskParams.keyPairName),
                any(KeyType.class),
                eq(taskParams.sshUser),
                eq(taskParams.sshPort),
                eq(taskParams.airGapInstall),
                eq(false),
                eq(taskParams.setUpChrony),
                eq(taskParams.ntpServers),
                eq(taskParams.showSetUpChrony),
                eq(false));
      } else {
        String expectedAccessKeyCode = taskParams.keyPairName;

        if (Strings.isNullOrEmpty(expectedAccessKeyCode)) {
          expectedAccessKeyCode = AccessKey.getDefaultKeyCode(provider);
        }

        verify(mockAccessManager, times(1))
            .addKey(
                eq(r.uuid),
                eq(expectedAccessKeyCode),
                any(),
                eq(taskParams.sshUser),
                eq(taskParams.sshPort),
                eq(taskParams.airGapInstall),
                eq(false),
                eq(taskParams.setUpChrony),
                eq(taskParams.ntpServers),
                eq(taskParams.showSetUpChrony));
      }
      // Check AZ info.
      List<AvailabilityZone> zones = r.zones;
      assertNotNull(zones);
      if (customAzMapping) {
        if (expectedProviderCode.equals("aws")) {
          assertEquals(metadata.azToSubnetIds.size(), zones.size());
          for (AvailabilityZone zone : zones) {
            String subnet = metadata.azToSubnetIds.get(zone.code);
            String subnetDb = zone.subnet;
            assertNotNull(subnetDb);
            assertEquals(subnet, subnetDb);
            if (hasSecondarySubnet) {
              String secondarySubnet = metadata.azToSecondarySubnetIds.get(zone.code);
              String secondarySubnetDb = zone.secondarySubnet;
              assertNotNull(secondarySubnetDb);
              assertEquals(secondarySubnet, secondarySubnetDb);
            } else {
              assertNull(zone.secondarySubnet);
            }
          }
        } else if (expectedProviderCode.equals("gcp")) {
          for (AvailabilityZone zone : zones) {
            assertEquals(metadata.subnetId, zone.subnet);
            if (hasSecondarySubnet) {
              assertEquals(metadata.secondarySubnetId, zone.secondarySubnet);
            } else {
              assertNull(zone.secondarySubnet);
            }
          }
        }

      } else {
        // By default, assume the test puts just 1 in the zoneInfo.
        assertEquals(1, zones.size());
      }
      // Check SG info.
      if (customSecurityGroup) {
        assertEquals(r.getSecurityGroupId(), metadata.customSecurityGroupId);
      }
      // Check AMI info.
      if (customImageId) {
        assertEquals(r.getYbImage(), metadata.customImageId);
      } else {
        assertEquals(r.getYbImage(), defaultImage);
      }
    }
  }

  @Test
  public void testCloudBootstrapSuccessAwsDefaultSingleRegion() throws InterruptedException {
    JsonNode zoneInfo = Json.parse("{\"us-west-1\": {\"zone-1\": \"subnet-1\"}}");
    CloudBootstrap.Params taskParams = getBaseTaskParams();
    CloudBootstrap.Params.PerRegionMetadata perRegionMetadata =
        new CloudBootstrap.Params.PerRegionMetadata();
    perRegionMetadata.vpcId = "test-vpc";
    taskParams.perRegionMetadata.put("us-west-1", perRegionMetadata);
    validateCloudBootstrapSuccess(
        taskParams, zoneInfo, ImmutableList.of("us-west-1"), "aws", false, false, false, false);
  }

  @Test
  public void testCloudBootstrapSuccessAwsDefaultSingleRegionCustomAccess()
      throws InterruptedException {
    JsonNode zoneInfo = Json.parse("{\"us-west-1\": {\"zone-1\": \"subnet-1\"}}");
    CloudBootstrap.Params taskParams = getBaseTaskParams();
    CloudBootstrap.Params.PerRegionMetadata perRegionMetadata =
        new CloudBootstrap.Params.PerRegionMetadata();
    perRegionMetadata.vpcId = "test-vpc";
    taskParams.perRegionMetadata.put("us-west-1", perRegionMetadata);
    // Add in the keypair info.
    taskParams.keyPairName = "keypair-name";
    taskParams.sshPrivateKeyContent = "ssh-content";
    taskParams.sshUser = "ssh-user";
    validateCloudBootstrapSuccess(
        taskParams, zoneInfo, ImmutableList.of("us-west-1"), "aws", true, false, false, false);
  }

  @Test
  public void testCloudBootstrapSuccessAwsDefaultSingleRegionCustomAccessIncomplete()
      throws InterruptedException {
    JsonNode zoneInfo = Json.parse("{\"us-west-1\": {\"zone-1\": \"subnet-1\"}}");
    CloudBootstrap.Params taskParams = getBaseTaskParams();
    CloudBootstrap.Params.PerRegionMetadata perRegionMetadata =
        new CloudBootstrap.Params.PerRegionMetadata();
    perRegionMetadata.vpcId = "test-vpc";
    taskParams.perRegionMetadata.put("us-west-1", perRegionMetadata);
    // Add in the keypair info.
    taskParams.keyPairName = "keypair-name";
    // Leave out one required component, expect to ignore.
    // taskParams.sshPrivateKeyContent = "ssh-content";
    taskParams.sshUser = "ssh-user";
    validateCloudBootstrapSuccess(
        taskParams, zoneInfo, ImmutableList.of("us-west-1"), "aws", false, false, false, false);
  }

  public void createPerRegionMetadata(
      String region,
      boolean useSecondarySubnet,
      boolean useCustomImage,
      CloudBootstrap.Params.PerRegionMetadata regionMetadata) {
    regionMetadata.vpcId = region;
    regionMetadata.azToSubnetIds = new HashMap<>();
    regionMetadata.azToSecondarySubnetIds = new HashMap<>();
    regionMetadata.azToSubnetIds.put(region + "-1a", "subnet-1");
    regionMetadata.azToSubnetIds.put(region + "-1b", "subnet-2");
    if (useSecondarySubnet) {
      regionMetadata.azToSecondarySubnetIds.put(region + "-1a", "subnet-1");
      regionMetadata.azToSecondarySubnetIds.put(region + "-1b", "subnet-2");
    }
    if (useCustomImage) {
      regionMetadata.customImageId = region + "-image";
    }
    regionMetadata.customSecurityGroupId = region + "-sg-id";
  }

  @Test
  public void testCloudBootstrapSuccessAwsCustomMultiRegion() throws InterruptedException {
    // Zone information should not be used if we are passing in custom azToSubnetIds mapping.
    JsonNode zoneInfo = Json.parse("{}");
    CloudBootstrap.Params taskParams = getBaseTaskParams();
    // Add region west.
    CloudBootstrap.Params.PerRegionMetadata westRegion =
        new CloudBootstrap.Params.PerRegionMetadata();
    createPerRegionMetadata("us-west", false, true, westRegion);
    // Add region east.
    CloudBootstrap.Params.PerRegionMetadata eastRegion =
        new CloudBootstrap.Params.PerRegionMetadata();
    createPerRegionMetadata("us-east", false, true, eastRegion);
    // Add all the regions in the taskParams and validate.
    taskParams.perRegionMetadata.put("us-west-1", westRegion);
    taskParams.perRegionMetadata.put("us-east-1", eastRegion);
    // Add in the keypair info.
    taskParams.keyPairName = "keypair-name";
    taskParams.sshPrivateKeyContent = "ssh-content";
    taskParams.sshUser = "ssh-user";
    validateCloudBootstrapSuccess(
        taskParams,
        zoneInfo,
        ImmutableList.of("us-west-1", "us-east-1"),
        "aws",
        true,
        true,
        true,
        true);
  }

  @Test
  public void testCloudBootstrapSuccessAwsCustomMultiRegionSecondarySubnet()
      throws InterruptedException {
    // Zone information should not be used if we are passing in custom azToSubnetIds mapping.
    JsonNode zoneInfo = Json.parse("{}");
    CloudBootstrap.Params taskParams = getBaseTaskParams();
    // Add region west.
    CloudBootstrap.Params.PerRegionMetadata westRegion =
        new CloudBootstrap.Params.PerRegionMetadata();
    createPerRegionMetadata("us-west", true, true, westRegion);
    // Add region east.
    CloudBootstrap.Params.PerRegionMetadata eastRegion =
        new CloudBootstrap.Params.PerRegionMetadata();
    createPerRegionMetadata("us-east", true, true, eastRegion);
    // Add all the regions in the taskParams and validate.
    taskParams.perRegionMetadata.put("us-west-1", westRegion);
    taskParams.perRegionMetadata.put("us-east-1", eastRegion);
    // Add in the keypair info.
    taskParams.keyPairName = "keypair-name";
    taskParams.sshPrivateKeyContent = "ssh-content";
    taskParams.sshUser = "ssh-user";
    validateCloudBootstrapSuccess(
        taskParams,
        zoneInfo,
        ImmutableList.of("us-west-1", "us-east-1"),
        "aws",
        true,
        true,
        true,
        true,
        true);
  }

  @Test
  public void testCloudBootstrapSuccessAwsCustomSingleRegionJustAzs() throws InterruptedException {
    // Zone information should not be used if we are passing in custom azToSubnetIds mapping.
    JsonNode zoneInfo = Json.parse("{}");
    CloudBootstrap.Params taskParams = getBaseTaskParams();
    // Add region west.
    CloudBootstrap.Params.PerRegionMetadata westRegion =
        new CloudBootstrap.Params.PerRegionMetadata();
    createPerRegionMetadata("us-west", false, false, westRegion);
    // Add all the regions in the taskParams and validate.
    taskParams.perRegionMetadata.put("us-west-1", westRegion);
    validateCloudBootstrapSuccess(
        taskParams, zoneInfo, ImmutableList.of("us-west-1"), "aws", false, true, false, false);
  }

  @Test
  public void testCloudBootstrapSuccessAwsCustomSingleRegionJustAzsSecondarySubnet()
      throws InterruptedException {
    // Zone information should not be used if we are passing in custom azToSubnetIds mapping.
    JsonNode zoneInfo = Json.parse("{}");
    CloudBootstrap.Params taskParams = getBaseTaskParams();
    // Add region west.
    CloudBootstrap.Params.PerRegionMetadata westRegion =
        new CloudBootstrap.Params.PerRegionMetadata();
    createPerRegionMetadata("us-west", true, false, westRegion);
    // Add all the regions in the taskParams and validate.
    taskParams.perRegionMetadata.put("us-west-1", westRegion);
    validateCloudBootstrapSuccess(
        taskParams,
        zoneInfo,
        ImmutableList.of("us-west-1"),
        "aws",
        false,
        true,
        false,
        false,
        true);
  }

  @Test
  public void testCloudBootstrapSuccessGcp() throws InterruptedException {
    JsonNode zoneInfo =
        Json.parse("{\"us-west1\": {\"zones\": [\"zone-1\"], \"subnetworks\": [\"subnet-0\"]}}");
    CloudBootstrap.Params taskParams = getBaseTaskParams();
    taskParams.providerUUID = gcpProvider.uuid;
    taskParams.perRegionMetadata.put("us-west1", new CloudBootstrap.Params.PerRegionMetadata());
    validateCloudBootstrapSuccess(
        taskParams, zoneInfo, ImmutableList.of("us-west1"), "gcp", false, false, false, false);
  }

  @Test
  public void testCloudBootstrapSuccessGcpCustom() throws InterruptedException {
    JsonNode zoneInfo =
        Json.parse("{\"us-west1\": {\"zones\": [\"zone-1\"], \"subnetworks\": [\"subnet-0\"]}}");
    CloudBootstrap.Params taskParams = getBaseTaskParams();
    CloudBootstrap.Params.PerRegionMetadata westRegion =
        new CloudBootstrap.Params.PerRegionMetadata();
    westRegion.subnetId = "us-west1";
    taskParams.providerUUID = gcpProvider.uuid;
    taskParams.perRegionMetadata.put("us-west1", westRegion);
    validateCloudBootstrapSuccess(
        taskParams, zoneInfo, ImmutableList.of("us-west1"), "gcp", false, true, false, false);
  }

  @Test
  public void testCloudBootstrapSuccessGcpCustomSecondarySubnet() throws InterruptedException {
    JsonNode zoneInfo =
        Json.parse("{\"us-west1\": {\"zones\": [\"zone-1\"], \"subnetworks\": [\"subnet-0\"]}}");
    CloudBootstrap.Params taskParams = getBaseTaskParams();
    CloudBootstrap.Params.PerRegionMetadata westRegion =
        new CloudBootstrap.Params.PerRegionMetadata();
    westRegion.subnetId = "us-west1-subnet1";
    westRegion.secondarySubnetId = "us-west1-subnet2";
    taskParams.providerUUID = gcpProvider.uuid;
    taskParams.perRegionMetadata.put("us-west1", westRegion);
    validateCloudBootstrapSuccess(
        taskParams, zoneInfo, ImmutableList.of("us-west1"), "gcp", false, true, false, false, true);
  }

  @Test
  public void testCloudBootstrapAddRegion() throws InterruptedException {
    JsonNode zoneInfo =
        Json.parse("{\"us-west1\": {\"zones\": [\"zone-1\"], \"subnetworks\": [\"subnet-0\"]}}");
    CloudBootstrap.Params taskParams = getBaseTaskParams();
    CloudBootstrap.Params.PerRegionMetadata westRegion =
        new CloudBootstrap.Params.PerRegionMetadata();
    westRegion.subnetId = "us-west1-subnet1";
    westRegion.secondarySubnetId = "us-west1-subnet2";
    taskParams.providerUUID = gcpProvider.uuid;
    taskParams.perRegionMetadata.put("us-west1", westRegion);
    validateCloudBootstrapSuccess(
        taskParams, zoneInfo, ImmutableList.of("us-west1"), "gcp", false, true, false, false, true);

    // Since the above was a success, there must now be an AccessKey created for the provider.
    // Create it so that it exists for the next call.
    String accessKeyCode = AccessKey.getDefaultKeyCode(gcpProvider);
    AccessKey.create(gcpProvider.uuid, accessKeyCode, new KeyInfo());

    // Add new region
    zoneInfo =
        Json.parse("{\"us-east1\": {\"zones\": [\"zone-1\"], \"subnetworks\": [\"subnet-1\"]}}");
    taskParams = getBaseTaskParams();
    CloudBootstrap.Params.PerRegionMetadata eastRegion =
        new CloudBootstrap.Params.PerRegionMetadata();
    eastRegion.subnetId = "us-east1-subnet1";
    eastRegion.secondarySubnetId = "us-east1-subnet2";
    taskParams.providerUUID = gcpProvider.uuid;
    taskParams.regionAddOnly = true;
    taskParams.perRegionMetadata.put("us-east1", eastRegion);
    validateCloudBootstrapSuccess(
        taskParams,
        zoneInfo,
        ImmutableList.of("us-east1"),
        "gcp",
        false,
        true,
        false,
        false,
        true,
        true);
  }

  @Test
  public void testCloudBootstrapWithInvalidRegion() throws InterruptedException {
    CloudBootstrap.Params taskParams = getBaseTaskParams();
    taskParams.perRegionMetadata.put("fake-region", new CloudBootstrap.Params.PerRegionMetadata());
    UUID taskUUID = submitTask(taskParams);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(Failure, taskInfo.getTaskState());
  }

  @Test
  public void testCloudBootstrapWithExistingRegion() throws InterruptedException {
    Region.create(defaultProvider, "us-west-1", "US west 1", "yb-image");
    CloudBootstrap.Params taskParams = getBaseTaskParams();
    taskParams.perRegionMetadata.put("us-west-1", new CloudBootstrap.Params.PerRegionMetadata());
    UUID taskUUID = submitTask(taskParams);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(Failure, taskInfo.getTaskState());
  }

  @Test
  public void testCloudBootstrapWithNetworkBootstrapError() throws InterruptedException {
    JsonNode vpcInfo = Json.parse("{\"error\": \"Something failed\"}");
    when(mockNetworkManager.bootstrap(any(), any(), anyString())).thenReturn(vpcInfo);
    CloudBootstrap.Params taskParams = getBaseTaskParams();
    taskParams.perRegionMetadata.put("us-west-1", new CloudBootstrap.Params.PerRegionMetadata());
    UUID taskUUID = submitTask(taskParams);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(Failure, taskInfo.getTaskState());
    Region r = Region.getByCode(defaultProvider, "us-west-1");
    assertNull(r);
  }
}
