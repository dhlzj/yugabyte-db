import React, { FC, useContext, useState } from 'react';
import _ from 'lodash';
import { useQuery } from 'react-query';
import { browserHistory } from 'react-router';
import { UniverseFormContext } from './UniverseFormContainer';
import { UniverseForm } from './form/UniverseForm';
import { DeleteClusterModal } from './action-modals';
import { YBLoading } from '../../../../components/common/indicators';
import { api, QUERY_KEY } from './utils/api';
import { getPlacements } from './form/fields/PlacementsField/PlacementsFieldHelper';
import { editReadReplica, getAsyncCluster, getAsyncFormData, getUserIntent } from './utils/helpers';
import { CloudType, ClusterModes, ClusterType, UniverseFormData } from './utils/dto';

interface EditReadReplicaProps {
  uuid: string;
}

export const EditReadReplica: FC<EditReadReplicaProps> = ({ uuid }) => {
  const [contextState, contextMethods] = useContext(UniverseFormContext);
  const { initializeForm, setUniverseResourceTemplate } = contextMethods;
  const [showDeleteRRModal, setShowDeleteRRModal] = useState(false);

  const { isLoading, data: universe } = useQuery(
    [QUERY_KEY.fetchUniverse, uuid],
    () => api.fetchUniverse(uuid),
    {
      onSuccess: async (resp) => {
        initializeForm({
          clusterType: ClusterType.ASYNC,
          mode: ClusterModes.EDIT,
          universeConfigureTemplate: _.cloneDeep(resp.universeDetails)
        });
        //set Universe Resource Template
        const resourceResponse = await api.universeResource(_.cloneDeep(resp.universeDetails));
        setUniverseResourceTemplate(resourceResponse);
      },
      onError: (err) => {
        console.error(err);
      }
    }
  );

  const onCancel = () => browserHistory.goBack();

  const onSubmit = (formData: UniverseFormData) => {
    const configurePayload = {
      ...contextState.universeConfigureTemplate,
      clusterOperation: ClusterModes.EDIT,
      currentClusterType: ClusterType.ASYNC,
      expectedUniverseVersion: universe?.version,
      clusters: [
        {
          ...getAsyncCluster(contextState.universeConfigureTemplate),
          userIntent: getUserIntent({ formData }),
          placementInfo: {
            cloudList: [
              {
                uuid: formData.cloudConfig.provider?.uuid as string,
                code: formData.cloudConfig.provider?.code as CloudType,
                regionList: getPlacements(formData)
              }
            ]
          }
        }
      ]
    };

    editReadReplica(configurePayload);
  };

  if (isLoading || contextState.isLoading) return <YBLoading />;

  if (!universe?.universeDetails) return null;

  //get async form data and intitalize the form
  const initialFormData = getAsyncFormData(universe.universeDetails);

  return (
    <>
      {showDeleteRRModal && (
        <DeleteClusterModal
          open={showDeleteRRModal}
          universeData={universe.universeDetails}
          onClose={() => setShowDeleteRRModal(false)}
        />
      )}
      <UniverseForm
        defaultFormData={initialFormData}
        onFormSubmit={(data: UniverseFormData) => onSubmit(data)}
        onCancel={onCancel}
        onDeleteRR={() => setShowDeleteRRModal(true)} //Deleting existing RR (API)
      />
    </>
  );
};
