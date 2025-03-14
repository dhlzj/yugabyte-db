/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import io.ebean.Ebean;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.SqlUpdate;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.EmbeddedId;
import javax.persistence.Entity;
import javax.persistence.Temporal;
import javax.persistence.TemporalType;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;
import play.libs.Json;

@Entity
// @IdClass(KmsHistoryId.class)
@ApiModel(description = "KMS history")
public class KmsHistory extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(KmsHistory.class);

  public static final int SCHEMA_VERSION = 1;

  @EmbeddedId
  @ApiModelProperty(value = "KMS history UUID", accessMode = READ_ONLY)
  public KmsHistoryId uuid;

  @Constraints.Required
  @Temporal(TemporalType.TIMESTAMP)
  @Column(nullable = false)
  @ApiModelProperty(value = "Timestamp of KMS history", accessMode = READ_ONLY)
  public Date timestamp;

  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(value = "Version of KMS history", accessMode = READ_ONLY)
  public int version;

  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(value = "KMS configuration UUID", accessMode = READ_ONLY)
  public UUID configUuid;

  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(value = "True if the KMS is active", accessMode = READ_ONLY)
  public boolean active;

  public static final Finder<KmsHistoryId, KmsHistory> find =
      new Finder<KmsHistoryId, KmsHistory>(KmsHistory.class) {};

  public static KmsHistory createKmsHistory(
      UUID configUUID, UUID targetUUID, KmsHistoryId.TargetType targetType, String keyRef) {
    KmsHistory keyHistory = new KmsHistory();
    keyHistory.uuid = new KmsHistoryId(keyRef, targetUUID, targetType);
    keyHistory.timestamp = new Date();
    keyHistory.version = SCHEMA_VERSION;
    keyHistory.active = false;
    keyHistory.configUuid = configUUID;
    keyHistory.save();
    return keyHistory;
  }

  public static void setKeyRefStatus(
      UUID targetUUID,
      UUID confidUUID,
      KmsHistoryId.TargetType targetType,
      String keyRef,
      boolean active) {
    String sql =
        "UPDATE kms_history"
            + " SET active = ?"
            + " WHERE target_uuid = ?"
            + " AND config_uuid = ?"
            + " AND type = ?"
            + " AND key_ref = ?";
    SqlUpdate update =
        Ebean.createSqlUpdate(sql)
            .setParameter(1, active)
            .setParameter(2, targetUUID)
            .setParameter(3, confidUUID)
            .setParameter(4, targetType)
            .setParameter(5, keyRef);
    int rows = update.execute();
    LOG.debug(
        String.format("setKeyRefStatus kms_history: Updating active status for %d rows", rows));
  }

  public static void activateKeyRef(
      UUID targetUUID, UUID configUUID, KmsHistoryId.TargetType targetType, String keyRef) {
    Ebean.beginTransaction();
    try {
      KmsHistory currentlyActiveKeyRef = KmsHistory.getActiveHistory(targetUUID, targetType);
      if (currentlyActiveKeyRef != null) {
        setKeyRefStatus(
            targetUUID,
            currentlyActiveKeyRef.configUuid,
            targetType,
            currentlyActiveKeyRef.uuid.keyRef,
            false);
      }
      KmsHistory toBeActiveKeyRef =
          KmsHistory.getKeyRefConfig(targetUUID, configUUID, keyRef, targetType);
      if (toBeActiveKeyRef != null) {
        setKeyRefStatus(targetUUID, toBeActiveKeyRef.configUuid, targetType, keyRef, true);
      }
      Ebean.commitTransaction();
    } finally {
      Ebean.endTransaction();
    }
  }

  public static List<KmsHistory> getAllConfigTargetKeyRefs(
      UUID configUUID, UUID targetUUID, KmsHistoryId.TargetType type) {
    return KmsHistory.find
        .query()
        .where()
        .eq("config_uuid", configUUID)
        .eq("target_uuid", targetUUID)
        .eq("type", type)
        .orderBy()
        .desc("timestamp")
        .findList();
  }

  public static List<KmsHistory> getAllTargetKeyRefs(
      UUID targetUUID, KmsHistoryId.TargetType type) {
    return KmsHistory.find
        .query()
        .where()
        .eq("target_uuid", targetUUID)
        .eq("type", type)
        .orderBy()
        .desc("timestamp")
        .findList();
  }

  public static KmsHistory getKeyRefConfig(
      UUID targetUUID, UUID configUUID, String keyRef, KmsHistoryId.TargetType type) {
    return KmsHistory.find
        .query()
        .where()
        .idEq(new KmsHistoryId(keyRef, targetUUID, type))
        .eq("config_uuid", configUUID)
        .eq("type", type)
        .findOne();
  }

  public static KmsHistory getActiveHistory(UUID targetUUID, KmsHistoryId.TargetType type) {
    return KmsHistory.find
        .query()
        .where()
        .eq("target_uuid", targetUUID)
        .eq("type", type)
        .eq("active", true)
        .findOne();
  }

  public static List<KmsHistory> getAllActiveHistory(KmsHistoryId.TargetType type) {
    return KmsHistory.find.query().where().eq("type", type).eq("active", true).findList();
  }

  public static boolean entryExists(UUID targetUUID, String keyRef, KmsHistoryId.TargetType type) {
    return KmsHistory.find
        .query()
        .where()
        .idEq(new KmsHistoryId(keyRef, targetUUID, type))
        .exists();
  }

  public static KmsHistory getLatestConfigHistory(
      UUID targetUUID, UUID configUUID, KmsHistoryId.TargetType type) {
    KmsHistory latestConfigHistory = null;
    List<KmsHistory> configKeyHistory =
        KmsHistory.getAllConfigTargetKeyRefs(configUUID, targetUUID, type);
    if (configKeyHistory.size() > 0) {
      latestConfigHistory = configKeyHistory.get(0);
    }
    return latestConfigHistory;
  }

  public static void deleteKeyRef(KmsHistory keyHistory) {
    keyHistory.delete();
  }

  public static void deleteAllTargetKeyRefs(UUID targetUUID, KmsHistoryId.TargetType type) {
    getAllTargetKeyRefs(targetUUID, type).forEach(KmsHistory::deleteKeyRef);
  }

  public static int deleteAllConfigTargetKeyRefs(
      UUID configUUID, UUID targetUUID, KmsHistoryId.TargetType type) {
    List<KmsHistory> keyList = getAllConfigTargetKeyRefs(configUUID, targetUUID, type);
    int count = keyList.size();
    keyList.forEach(KmsHistory::deleteKeyRef);
    return count;
  }

  public static boolean configHasHistory(UUID configUUID, KmsHistoryId.TargetType type) {
    return KmsHistory.find
            .query()
            .where()
            .eq("config_uuid", configUUID)
            .eq("type", type)
            .findList()
            .size()
        != 0;
  }

  public static Set<Universe> getUniverses(UUID configUUID, KmsHistoryId.TargetType type) {
    Set<UUID> universeUUIDs = new HashSet<>();
    KmsHistory.find
        .query()
        .where()
        .eq("config_uuid", configUUID)
        .eq("type", type)
        .findList()
        .forEach(n -> universeUUIDs.add(n.uuid.targetUuid));
    return Universe.getAllPresent(universeUUIDs);
  }

  @Override
  public String toString() {
    return Json.newObject()
        .put("uuid", uuid.toString())
        .put("config_uuid", configUuid.toString())
        .put("timestamp", timestamp.toString())
        .put("version", version)
        .put("active", active)
        .toString();
  }
}
