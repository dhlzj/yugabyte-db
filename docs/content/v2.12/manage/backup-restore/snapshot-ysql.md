---
title: Snapshot and restore data for YSQL
headerTitle: Snapshot and restore data
linkTitle: Snapshot and restore data
description: Snapshot and restore data in YugabyteDB for YSQL.
image: /images/section_icons/manage/enterprise.png
menu:
  v2.12:
    identifier: snapshots-1-ysql
    parent: backup-restore
    weight: 705
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/preview/manage/backup-restore/snapshot-ysql" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="/preview/manage/backup-restore/snapshots-ycql" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

YugabyteDB supports distributed backup and restore of YSQL databases. Backing up and restoring of individual tables within a database is not yet supported.

## Create a snapshot for a YSQL database

1. Get the current YSQL schema catalog version by running the following [`yb-admin ysql_catalog_version`](../../../admin/yb-admin/#ysql-catalog-version) command.

    ```sh
    yb-admin -master_addresses <ip1:7100,ip2:7100,ip3:7100> ysql_catalog_version
    ```

    ```output
    Version:1
    ```

1. Create a database snapshot using [`yb-admin create_database_snapshot`](../../../admin/yb-admin/#create-database-snapshot) command:

    ```sh
    yb-admin -master_addresses <ip1:7100,ip2:7100,ip3:7100> create_database_snapshot ysql.<database_name>
    ```

    The output contains the snapshot ID. For example:

    ```output
    Started snapshot creation: 0d4b4935-2c95-4523-95ab-9ead1e95e794
    ```

1. Run the [`yb-admin list_snapshots`](../../../admin/yb-admin/#list_snapshots) command to verify that the snapshot is complete. (If the snapshot creation is not complete, re-run the command until the state shows `COMPLETE`.)

    ```sh
    yb-admin -master_addresses <ip1:7100,ip2:7100,ip3:7100> list_snapshots
    ```

    The output shows the snapshot UUID and the current state.

    ```output
    Snapshot UUID                     State
    4963ed18fc1e4f1ba38c8fcf4058b295  COMPLETE
    ```

1. Create a backup of the YSQL metadata, including the schema, by running the following `ysql_dump --create` command:

    ```sh
    ysql_dump -h <ip> --masters <ip1:7100,ip2:7100,ip3:7100> --include-yb-metadata --serializable-deferrable --create --schema-only --dbname <database_name> --file ysql.schema.sql
    ```

1. Get the current YSQL schema catalog version by running the [`yb-admin ysql_catalog_version`](../../../admin/yb-admin/#ysql-catalog-version) command:

    ```sh
    yb-admin -master_addresses <ip1:7100,ip2:7100,ip3:7100> ysql_catalog_version
    ```

    The output displays the version:

    ```output
    Version: 1
    ```

    If the version number here is greater than the version number from the first step, repeat all the steps to this point. The snapshot process can only guarantee consistency when no schema changes are made while taking the snapshot and the version number has remained the same.

1. Create the snapshot metadata file. Export the snapshot metadata using the following [`yb-admin export_snapshot`](../../../admin/yb-admin/#export-snapshot) command:

    ```sh
    ./bin/yb-admin export_snapshot 0d4b4935-2c95-4523-95ab-9ead1e95e794 test.snapshot
    ```

1. Copy the YSQL metadata dump and the snapshot metadata files to a snapshot directory. Create a snapshot directory and copy the YSQL metadata dump (created in step 4) and the snapshot metadata (created in step 6) into the directory.

    ```sh
    mkdir snapshot
    cp test.snapshot ysql.schema.sql snapshot/
    ```

1. Copy the tablet snapshots into the `snapshot` directory. Do this for all tablets of all tables in the database.

    ```sh
    cp -r ~/yugabyte-data/node-1/disk-1/yb-data/tserver/data/rocksdb/table-00004000000030008000000000004003/tablet-b0de9bc6a4cb46d4aaacf4a03bcaf6be.snapshots snapshot/
    ```

    The file path structure is:

    ```output
    <yb_data_dir>/node-<node_number>/disk-<disk_number>/yb-data/tserver/data/rocksdb/table-<table_id>/[tablet-<tablet_id>.snapshots]/<snapshot_id>
    ```

    - `<yb_data_dir>` is the directory where YugabyteDB data is stored. (default=`~/yugabyte-data`)
    - `<node_number>` is used when multiple nodes are running on the same server (for testing, QA, and development). The default value is `1`.
    - `<disk_number>` when running YugabyteDB on multiple disks with the `--fs_data_dirs` flag. The default value is `1`.
    - `<table_id>` is the UUID of the table. You can get it from the `http://<yb-master-ip>:7000/tables` url in the Admin
    UI.
    - `<tablet_id>` in each table there is a list of tablets. Each tablet has a `<tablet_id>.snapshots` directory that you need to copy.
    - `<snapshot_id>` there is a directory for each snapshot since you can have multiple completed snapshots on each server.

    In practice, for each server, you will use the `--fs_data_dirs` flag, which is a comma-separated list of paths where to put the data (normally different paths should be on different disks).

    {{< note title="Tip" >}}

To get a snapshot of a multi-node cluster, you need to go into each node and copy
the folders of ONLY the leader tablets on that node. Because each tablet-replica has a copy of the same data, you do not need to keep a copy for each replica.

    {{< /note >}}

Your snapshot of the YSQL database is complete.

-----

## Restore a snapshot

Let’s destroy the existing cluster, create a new cluster, and import the snapshot that we had previously created.

1. Import the YSQL metadata.

    ```sh
    ./bin/ysqlsh -h 127.0.0.1 --echo-all --file=snapshot/ysql.schema.sql
    ```

1. Import the snapshot metadata.

    ```sh
    ./bin/yb-admin import_snapshot snapshot/test.snapshot <database_name>
    ```

    The output contains the mapping between the old tablet IDs and the new tablet IDs.

    ```output
    Read snapshot meta file snapshot/test.snapshot
    Importing snapshot 0d4b4935-2c95-4523-95ab-9ead1e95e794 (COMPLETE)
    Table type: table
    Target imported table name: test.t1
    Table being imported: test.t1
    Table type: table
    Target imported table name: test.t2
    Table being imported: test.t2
    Successfully applied snapshot.
    Object           Old ID                                 New ID
    Keyspace         00004000000030008000000000000000       00004000000030008000000000000000
    Table            00004000000030008000000000004003       00004000000030008000000000004001
    Tablet 0         b0de9bc6a4cb46d4aaacf4a03bcaf6be       50046f422aa6450ca82538e919581048
    Tablet 1         27ce76cade8e4894a4f7ffa154b33c3b       111ab9d046d449d995ee9759bf32e028
    Snapshot         0d4b4935-2c95-4523-95ab-9ead1e95e794   6beb9c0e-52ea-4f61-89bd-c160ec02c729
    ```

1. Copy the tablet snapshots.

    Use the tablet mappings to copy the tablet snapshot files from `snapshot` directory to appropriate location.

    ```sh
    yb-data/tserver/data/rocksdb/table-<tableid>/tablet-<tabletid>.snapshots
    ```

    In our example, it'll be:

    ```sh
    cp -r snapshot/tablet-b0de9bc6a4cb46d4aaacf4a03bcaf6be.snapshots/0d4b4935-2c95-4523-95ab-9ead1e95e794 \
        ~/yugabyte-data-restore/node-1/disk-1/yb-data/tserver/data/rocksdb/table-00004000000030008000000000004001/tablet-50046f422aa6450ca82538e919581048.snapshots/6beb9c0e-52ea-4f61-89bd-c160ec02c729
    ```

    ```sh
    cp -r snapshot/tablet-27ce76cade8e4894a4f7ffa154b33c3b.snapshots/0d4b4935-2c95-4523-95ab-9ead1e95e794 \
        ~/yugabyte-data-restore/node-1/disk-1/yb-data/tserver/data/rocksdb/table-00004000000030008000000000004001/tablet-111ab9d046d449d995ee9759bf32e028.snapshots/6beb9c0e-52ea-4f61-89bd-c160ec02c729
    ```

    {{< note title="Note" >}}

For each tablet, you need to copy the snapshots folder on all tablet peers and in any configured read replica cluster.

    {{< /note >}}

1. To restore the snapshot, run the [`yb-admin restore_snapshot`](../../../admin/yb-admin/#restore-snapshot) command.

    ```sh
    yb-admin restore_snapshot <snapshot_id>
    ```

    Use the new snapshot ID from output of step 2.

    ```sh
    yb-admin restore_snapshot 6beb9c0e-52ea-4f61-89bd-c160ec02c729
    ```

    ```output
    Started restoring snapshot: 6beb9c0e-52ea-4f61-89bd-c160ec02c729
    Restoration id: aa7c413b-9d6b-420a-b994-6626980240ca
    ```

    To verify that the snapshot has successfully restored, run the [`yb-admin list_snapshots`](../../../admin/yb-admin/#list-snapshots) command, like this:

    ```sh
    yb-admin list_snapshots
    ```

    ```output
    Snapshot UUID                         State
    6beb9c0e-52ea-4f61-89bd-c160ec02c729  COMPLETE
    Restoration UUID                      State
    aa7c413b-9d6b-420a-b994-6626980240ca  RESTORED
    ```

1. Verify the data.

    ```sh
    ./bin/ysqlsh -h 127.0.0.1 -d test
    ```

    ```output
    ysqlsh (11.2-YB-2.2.1.0-b0)
    Type "help" for help.
    ```

    ```sql
    test=# \d
    ```

    ```output
                List of relations
    Schema |   Name   |   Type   |  Owner
    --------+----------+----------+----------
    public | t        | table    | yugabyte
    public | t_a_seq  | sequence | yugabyte
    (2 rows)
    ```

    ```sql
    test=# \d t
    ```

    ```output
                                Table "public.t"
    Column |  Type   | Collation | Nullable |            Default
    --------+---------+-----------+----------+-------------------------------
    a      | integer |           | not null | nextval('t_a_seq'::regclass)
    b      | integer |           |          |
    Indexes:
        "t_pkey" PRIMARY KEY, lsm (a HASH)
    ```

    ```sql
    test=# SELECT * from t;
    ```

    ```output
     a  |  b
    ----+-----
      5 | 105
      1 | 101
      6 | 106
      7 | 107
      9 | 109
     10 | 110
      4 | 104
      2 | 102
      8 | 108
      3 | 103
    (10 rows)
    ```

    If no longer needed, you can delete the snapshot and increase disk space.

    ```sh
    $ ./bin/yb-admin delete_snapshot 6beb9c0e-52ea-4f61-89bd-c160ec02c729
    ```

    The output should show that the snapshot is deleted.

    ```output
    Deleted snapshot: 6beb9c0e-52ea-4f61-89bd-c160ec02c729
    ```

{{< tip title="Automating backups and restores" >}}

To automate and simplify these manual backup creating and restoring steps, you can use the Python script `yb_backup.py`, located in the YugabyteDB GitHub repository:
[yugabyte/yugabyte-db/managed/devops/bin](https://github.com/yugabyte/yugabyte-db/tree/master/managed/devops/bin).

`yb_backup.py` performs all the steps described on this page and uses external storage to store and load the created snapshot. Currently, it supports the following external storage options: Azure-Storage, Google-Cloud-Storage, s3-Storage, and NFS. To access the cluster hosts, the script requires SSH access (except for single-node clusters using the `--no_ssh` script argument). The `--verbose` flag can help in setting up the script for your environment.

{{< /tip >}}
