/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.cdc.CdcStream;
import com.yugabyte.yw.common.cdc.CdcStreamCreateResponse;
import com.yugabyte.yw.common.cdc.CdcStreamDeleteResponse;
import com.yugabyte.yw.common.cdc.CdcStreamManager;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.CdcStreamFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import java.util.List;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.mvc.Result;

@Api
public class UniverseCdcStreamController extends AuthenticatedController {
  private static final Logger LOG = LoggerFactory.getLogger(UniverseCdcStreamController.class);

  @Inject private RuntimeConfigFactory runtimeConfigFactory;
  @Inject private CdcStreamManager cdcStreamManager;

  public Universe checkCloudAndValidateUniverse(UUID customerUUID, UUID universeUUID) {
    LOG.info("Checking config for customer='{}', universe='{}'", customerUUID, universeUUID);
    if (!runtimeConfigFactory.globalRuntimeConf().getBoolean("yb.cloud.enabled")) {
      throw new PlatformServiceException(
          METHOD_NOT_ALLOWED, "CDC Stream management is not available.");
    }

    Customer customer = Customer.getOrBadRequest(customerUUID);
    return Universe.getValidUniverseOrBadRequest(universeUUID, customer);
  }

  @ApiOperation(value = "List CDC Streams for a cluster", notes = "List CDC Streams for a cluster")
  public Result listCdcStreams(UUID customerUUID, UUID universeUUID) throws Exception {
    Universe universe = checkCloudAndValidateUniverse(customerUUID, universeUUID);

    List<CdcStream> response = cdcStreamManager.getAllCdcStreams(universe);
    return PlatformResults.withData(response);
  }

  @ApiOperation(
      value = "Create CDC Stream for a cluster",
      notes = "Create CDC Stream for a cluster")
  public Result createCdcStream(UUID customerUUID, UUID universeUUID) throws Exception {
    Universe universe = checkCloudAndValidateUniverse(customerUUID, universeUUID);

    Form<CdcStreamFormData> formData = formFactory.getFormDataOrBadRequest(CdcStreamFormData.class);
    CdcStreamFormData data = formData.get();

    // No need to check if database exists at this layer, as lower layers will try to
    // enumerate all tables, so they will fail.
    CdcStreamCreateResponse response =
        cdcStreamManager.createCdcStream(universe, data.getDatabaseName());
    return PlatformResults.withData(response);
  }

  @ApiOperation(
      value = "Delete a CDC stream for a cluster",
      notes = "Delete a CDC Stream for a cluster")
  public Result deleteCdcStream(UUID customerUUID, UUID universeUUID, String streamId)
      throws Exception {
    Universe universe = checkCloudAndValidateUniverse(customerUUID, universeUUID);

    CdcStreamDeleteResponse response = cdcStreamManager.deleteCdcStream(universe, streamId);
    return PlatformResults.withData(response);
  }
}
