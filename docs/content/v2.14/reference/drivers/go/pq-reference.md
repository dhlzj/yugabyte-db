---
title: Go Drivers
linkTitle: Go Drivers
description: Go Drivers for YSQL
headcontent: Go Drivers for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.14:
    name: Go Drivers
    identifier: ref-pq-go-driver
    parent: drivers
    weight: 620
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/preview/reference/drivers/go/yb-pgx-reference/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
       YugabyteDB PGX Driver
    </a>
  </li>

  <li >
    <a href="/preview/reference/drivers/go/pgx-reference/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PGX Driver
    </a>
  </li>

  <li >
    <a href="/preview/reference/drivers/go/pq-reference/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PQ Driver
    </a>
  </li>

</ul>

The [PQ driver](https://github.com/lib/pq/) is a popular driver for PostgreSQL which can used for connecting to YugabyteDB YSQL as well.

This driver allows Go programmers to connect to YugabyteDB database to execute DMLs and DDLs using
the standard `database/sql` package.

## Fundamentals of PQ Driver

Learn how to establish a connection to YugabyteDB database and begin simple CRUD operations using
the steps in the [Build an application](../../../../quick-start/build-apps/go/ysql-pq) page under the
Quick start section.

Let us break down the quick start example and understand how to perform the common tasks required
for Go App development using the PQ driver.

### Import the driver package

Import the PQ driver package by adding the following import statement in your Go code.

```go
import (
  _ "github.com/lib/pq"
)
```

### Connect to YugabyteDB database

Go Apps can connect to the YugabyteDB database using the `sql.Open()` function.
All the functions or structs required for working with YugabyteDB database are part of `sql` package.

Use the `sql.Open()` function for getting connection object for the YugabyteDB database which can be
used for performing DDLs and DMLs against the database.

The connection details can be specified either as string params or via an url in the format given below:

```go
postgresql://username:password@hostname:port/database
```

Code snippet for connecting to YugabyteDB:

```go
psqlInfo := fmt.Sprintf("host=%s port=%d user=%s password=%s dbname=%s",
                        host, port, user, password, dbname)
// Other connection configs are read from the standard environment variables:
// PGSSLMODE, PGSSLROOTCERT, and so on.
db, err := sql.Open("postgres", psqlInfo)
defer db.Close()
if err != nil {
    log.Fatal(err)
}
```

| Parameters | Description | Default |
| :---------- | :---------- | :------ |
| host  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte
| dbname | database name | yugabyte

### Create table

Execute an SQL statement like the DDL `CREATE TABLE ...` using the `Exec()` function on the `db`
instance.

The CREATE DDL statement:

```sql
CREATE TABLE employee (id int PRIMARY KEY, name varchar, age int, language varchar)
```

Code snippet:

```go
var createStmt = `CREATE TABLE employee (id int PRIMARY KEY,
                                         name varchar,
                                         age int,
                                         language varchar)`;
if _, err := db.Exec(createStmt); err != nil {
    log.Fatal(err)
}
```

The `db.Exec()` function also returns an `error` object which, if not `nil`, needs to handled in
your code.

Read more on designing [Database schemas and tables](../../../../explore/ysql-language-features/databases-schemas-tables/).

### Read and write data

#### Insert data

To write data into YugabyteDB, execute the `INSERT` statement using the same `db.Exec()` function.

The INSERT DML statement:

```sql
INSERT INTO employee(id, name, age, language) VALUES (1, 'John', 35, 'Go')
```

Code snippet:

```go
var insertStmt string = "INSERT INTO employee(id, name, age, language)" +
    " VALUES (1, 'John', 35, 'Go')";
if _, err := db.Exec(insertStmt); err != nil {
    log.Fatal(err)
}
```

#### Query data

To query data from YugabyteDB tables, execute the `SELECT` statement using the function `Query()` on `db` instance.

Query results are returned as `rows` which can be iterated using `rows.next()` method.
Use `rows.Scan()` for reading the data.

The SELECT DML statement:

```sql
SELECT * from employee;
```

Code snippet:

```go
var name string
var age int
var language string
rows, err := db.Query(`SELECT name, age, language FROM employee WHERE id = 1`)
if err != nil {
    log.Fatal(err)
}
defer rows.Close()
fmt.Printf("Query for id=1 returned: ");
for rows.Next() {
    err := rows.Scan(&name, &age, &language)
    if err != nil {
       log.Fatal(err)
    }
    fmt.Printf("Row[%s, %d, %s]\n", name, age, language)
}
err = rows.Err()
if err != nil {
    log.Fatal(err)
}
```

### Configure SSL/TLS

To build a Go application that communicates securely over SSL with YugabyteDB database,
you need the root certificate (`ca.crt`) of the YugabyteDB Cluster.
To generate these certificates and install them while launching the cluster, follow the instructions in
[Create server certificates](../../../../secure/tls-encryption/server-certificates/).

For a YugabyteDB Managed cluster, or a YugabyteDB cluster with SSL/TLS enabled, set the SSL-related
environment variables as below at the client side.

```sh
$ export PGSSLMODE=verify-ca
$ export PGSSLROOTCERT=~/root.crt  # Here, the CA certificate file is downloaded as `root.crt` under home directory. Modify your path accordingly.
```

| Environment Variable | Description |
| :---------- | :---------- |
| PGSSLMODE |  SSL mode used for the connection |
| PGSSLROOTCERT | Server CA Certificate |

#### SSL modes

| SSL Mode | Client Driver Behavior | YugabyteDB Support |
| :------- | :--------------------- | ------------------ |
| disable  | SSL Disabled | Supported
| allow    | SSL enabled only if server requires SSL connection | Not supported
| prefer (default) | SSL enabled only if server requires SSL connection | Not supported
| require | SSL enabled for data encryption and Server identity is not verified | Supported
| verify-ca | SSL enabled for data encryption and Server CA is verified | Supported
| verify-full | SSL enabled for data encryption. Both CA and hostname of the certificate are verified | Supported

### Transaction and isolation levels

YugabyteDB supports transactions for inserting and querying data from the tables. YugabyteDB
supports different [isolation levels](../../../../architecture/transactions/isolation-levels/) for
maintaining strong consistency for concurrent data access.

The PQ driver provides `db.Begin()` function to start a transaction.
Another function `conn.BeginEx()` can create a transaction with a specified isolation level.`

```go
tx, err := db.Begin()
if err != nil {
  log.Fatal(err)
}

...

_, err = stmt.Exec()
if err != nil {
  log.Fatal(err)
}

err = stmt.Close()
if err != nil {
  log.Fatal(err)
}

err = txn.Commit()
if err != nil {
  log.Fatal(err)
}
```
