import React, { FC, useContext, useState } from 'react';
import _ from 'lodash';
import { useQuery } from 'react-query';
import { browserHistory } from 'react-router';
import { UniverseFormContext } from './UniverseFormContainer';
import { UniverseForm } from './form/UniverseForm';
import { FullMoveModal, ResizeNodeModal, SmartResizeModal } from './action-modals';
import { YBLoading } from '../../../../components/common/indicators';
import { getPlacements } from './form/fields/PlacementsField/PlacementsFieldHelper';
import { api, QUERY_KEY } from './utils/api';
import { getPrimaryFormData, transformTagsArrayToObject, transitToUniverse } from './utils/helpers';
import {
  Cluster,
  ClusterModes,
  ClusterType,
  UniverseConfigure,
  UniverseFormData,
  UniverseDetails
} from './utils/dto';
import {
  DEVICE_INFO_FIELD,
  DEVICE_INFO_MASTER_FIELD,
  INSTANCE_TYPE_FIELD,
  INSTANCE_TYPE_MASTER_FIELD,
  REGIONS_FIELD,
  REPLICATION_FACTOR_FIELD,
  TOTAL_NODES_FIELD,
  USER_TAGS_FIELD
} from './utils/constants';

export enum UPDATE_ACTIONS {
  FULL_MOVE = 'FULL_MOVE',
  SMART_RESIZE = 'SMART_RESIZE',
  SMART_RESIZE_NON_RESTART = 'SMART_RESIZE_NON_RESTART',
  UPDATE = 'UPDATE'
}

interface EditUniverseProps {
  uuid: string;
}

export const EditUniverse: FC<EditUniverseProps> = ({ uuid }) => {
  const [contextState, contextMethods] = useContext(UniverseFormContext);
  const { isLoading, universeConfigureTemplate } = contextState;
  const { initializeForm, setUniverseResourceTemplate } = contextMethods;

  //Local states
  const [showFMModal, setFMModal] = useState(false); //FM -> Full Move
  const [showRNModal, setRNModal] = useState(false); //RN -> Resize Nodes
  const [showSRModal, setSRModal] = useState(false); //SR -> Smart Resize
  const [universePayload, setUniversePayload] = useState<UniverseDetails | null>(null);

  const { isLoading: isUniverseLoading, data: originalData } = useQuery(
    [QUERY_KEY.fetchUniverse, uuid],
    () => api.fetchUniverse(uuid),
    {
      onSuccess: async (resp) => {
        initializeForm({
          clusterType: ClusterType.PRIMARY,
          mode: ClusterModes.EDIT,
          universeConfigureTemplate: _.cloneDeep(resp.universeDetails)
        });
        //set Universe Resource Template
        const resourceResponse = await api.universeResource(_.cloneDeep(resp.universeDetails));
        setUniverseResourceTemplate(resourceResponse);
      },
      onError: (err) => {
        console.log(err);
      }
    }
  );

  const onCancel = () => browserHistory.goBack();

  const submitEditUniverse = async (finalPayload: UniverseConfigure) => {
    try {
      await api.editUniverse(finalPayload, uuid);
    } catch (e) {
      console.log(e);
    } finally {
      transitToUniverse(uuid);
    }
  };

  if (isUniverseLoading || isLoading || !originalData?.universeDetails) return <YBLoading />;

  const initialFormData = getPrimaryFormData(originalData.universeDetails);

  const onSubmit = async (formData: UniverseFormData) => {
    if (!_.isEqual(formData, initialFormData)) {
      const payload = _.cloneDeep(universeConfigureTemplate);
      payload.currentClusterType = ClusterType.PRIMARY;
      payload.clusterOperation = ClusterModes.EDIT;
      const primaryIndex = payload.clusters.findIndex(
        (c: Cluster) => c.clusterType === ClusterType.PRIMARY
      );
      //update fields which are allowed to edit
      const userIntent = payload.clusters[primaryIndex].userIntent;
      userIntent.regionList = _.get(formData, REGIONS_FIELD);
      userIntent.numNodes = _.get(formData, TOTAL_NODES_FIELD);
      userIntent.replicationFactor = _.get(formData, REPLICATION_FACTOR_FIELD);
      userIntent.instanceType = _.get(formData, INSTANCE_TYPE_FIELD);
      //  userIntent.instanceTypeMaster = _.get(formData, INSTANCE_TYPE_MASTER_FIELD);
      userIntent.deviceInfo = _.get(formData, DEVICE_INFO_FIELD);
      //  userIntent.deviceInfoMaster = _.get(formData, DEVICE_INFO_MASTER_FIELD);
      userIntent.instanceTags = transformTagsArrayToObject(_.get(formData, USER_TAGS_FIELD, []));
      payload.clusters[primaryIndex].placementInfo.cloudList[0].regionList = getPlacements(
        formData
      );
      const finalPayload = await api.universeConfigure(payload);
      const { updateOptions } = finalPayload;
      setUniversePayload(finalPayload);
      if (
        _.intersection(updateOptions, [UPDATE_ACTIONS.SMART_RESIZE, UPDATE_ACTIONS.FULL_MOVE])
          .length > 1
      )
        setSRModal(true);
      else if (updateOptions.includes(UPDATE_ACTIONS.SMART_RESIZE_NON_RESTART)) setRNModal(true);
      else if (updateOptions.includes(UPDATE_ACTIONS.FULL_MOVE)) setFMModal(true);
      else submitEditUniverse(finalPayload);
    } else console.log("'Nothing to update - no fields changed'");
  };

  return (
    <>
      <UniverseForm defaultFormData={initialFormData} onFormSubmit={onSubmit} onCancel={onCancel} />
      {universePayload && (
        <>
          {showRNModal && (
            <ResizeNodeModal
              open={showRNModal}
              universeData={universePayload}
              onClose={() => setRNModal(false)}
            />
          )}
          {showSRModal && (
            <SmartResizeModal
              open={showSRModal}
              oldConfigData={originalData.universeDetails}
              newConfigData={universePayload}
              onClose={() => setSRModal(false)}
              handleSmartResize={() => {
                setSRModal(false);
                setRNModal(true);
              }}
              handleFullMove={() => submitEditUniverse(universePayload)}
            />
          )}
          {showFMModal && (
            <FullMoveModal
              open={showFMModal}
              oldConfigData={originalData.universeDetails}
              newConfigData={universePayload}
              onClose={() => setFMModal(false)}
              onSubmit={() => submitEditUniverse(universePayload)}
            />
          )}
        </>
      )}
    </>
  );
};
