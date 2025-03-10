---
title: CREATE INDEX statement [YCQL]
headerTitle: CREATE INDEX
linkTitle: CREATE INDEX
summary: Create a new index on a table
description: Use the CREATE INDEX statement to create a new index on a table.
menu:
  v2.14:
    parent: api-cassandra
    weight: 1225
type: docs
---

## Synopsis

Use the `CREATE INDEX` statement to create a new index on a table. It defines the index name, index columns, and additional columns to include.

## Syntax

### Diagram

#### create_index

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="703" height="150" viewbox="0 0 703 150"><path class="connector" d="M0 22h15m67 0h10m59 0h30m32 0h10m45 0h10m64 0h20m-196 0q5 0 5 5v8q0 5 5 5h171q5 0 5-5v-8q0-5 5-5m5 0h10m94 0h10m38 0h7m2 0h2m2 0h2m-529 50h2m2 0h2m2 0h7m91 0h10m25 0h10m157 0h30m165 0h20m-200 0q5 0 5 5v8q0 5 5 5h175q5 0 5-5v-8q0-5 5-5m5 0h10m25 0h7m2 0h2m2 0h2m-573 50h2m2 0h2m2 0h27m216 0h20m-251 0q5 0 5 5v8q0 5 5 5h226q5 0 5-5v-8q0-5 5-5m5 0h30m128 0h20m-163 0q5 0 5 5v8q0 5 5 5h138q5 0 5-5v-8q0-5 5-5m5 0h30m65 0h10m114 0h20m-224 0q5 0 5 5v8q0 5 5 5h199q5 0 5-5v-8q0-5 5-5m5 0h15"/><polygon points="0,29 5,22 0,15" style="fill:black;stroke-width:0"/><rect class="literal" x="15" y="5" width="67" height="25" rx="7"/><text class="text" x="25" y="22">CREATE</text><rect class="literal" x="92" y="5" width="59" height="25" rx="7"/><text class="text" x="102" y="22">INDEX</text><rect class="literal" x="181" y="5" width="32" height="25" rx="7"/><text class="text" x="191" y="22">IF</text><rect class="literal" x="223" y="5" width="45" height="25" rx="7"/><text class="text" x="233" y="22">NOT</text><rect class="literal" x="278" y="5" width="64" height="25" rx="7"/><text class="text" x="288" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#index-name"><rect class="rule" x="372" y="5" width="94" height="25"/><text class="text" x="382" y="22">index_name</text></a><rect class="literal" x="476" y="5" width="38" height="25" rx="7"/><text class="text" x="486" y="22">ON</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="15" y="55" width="91" height="25"/><text class="text" x="25" y="72">table_name</text></a><rect class="literal" x="116" y="55" width="25" height="25" rx="7"/><text class="text" x="126" y="72">(</text><a xlink:href="../grammar_diagrams#partition-key-columns"><rect class="rule" x="151" y="55" width="157" height="25"/><text class="text" x="161" y="72">partition_key_columns</text></a><a xlink:href="../grammar_diagrams#clustering-key-columns"><rect class="rule" x="338" y="55" width="165" height="25"/><text class="text" x="348" y="72">clustering_key_columns</text></a><rect class="literal" x="533" y="55" width="25" height="25" rx="7"/><text class="text" x="543" y="72">)</text><a xlink:href="../grammar_diagrams#clustering-key-column-ordering"><rect class="rule" x="35" y="105" width="216" height="25"/><text class="text" x="45" y="122">clustering_key_column_ordering</text></a><a xlink:href="../grammar_diagrams#covering-columns"><rect class="rule" x="301" y="105" width="128" height="25"/><text class="text" x="311" y="122">covering_columns</text></a><rect class="literal" x="479" y="105" width="65" height="25" rx="7"/><text class="text" x="489" y="122">WHERE</text><a xlink:href="../grammar_diagrams#index-predicate"><rect class="rule" x="554" y="105" width="114" height="25"/><text class="text" x="564" y="122">index_predicate</text></a><polygon points="699,129 703,129 703,115 699,115" style="fill:black;stroke-width:0"/></svg>

#### partition_key_columns

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="286" height="95" viewbox="0 0 286 95"><path class="connector" d="M0 22h35m106 0h130m-251 0q5 0 5 5v50q0 5 5 5h5m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h5q5 0 5-5v-50q0-5 5-5m5 0h15"/><polygon points="0,29 5,22 0,15" style="fill:black;stroke-width:0"/><a xlink:href="../grammar_diagrams#index-column"><rect class="rule" x="35" y="5" width="106" height="25"/><text class="text" x="45" y="22">index_column</text></a><rect class="literal" x="35" y="65" width="25" height="25" rx="7"/><text class="text" x="45" y="82">(</text><rect class="literal" x="131" y="35" width="24" height="25" rx="7"/><text class="text" x="141" y="52">,</text><a xlink:href="../grammar_diagrams#index-column"><rect class="rule" x="90" y="65" width="106" height="25"/><text class="text" x="100" y="82">index_column</text></a><rect class="literal" x="226" y="65" width="25" height="25" rx="7"/><text class="text" x="236" y="82">)</text><polygon points="282,29 286,29 286,15 282,15" style="fill:black;stroke-width:0"/></svg>

#### clustering_key_columns

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="176" height="65" viewbox="0 0 176 65"><path class="connector" d="M0 52h35m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h35"/><polygon points="0,59 5,52 0,45" style="fill:black;stroke-width:0"/><rect class="literal" x="76" y="5" width="24" height="25" rx="7"/><text class="text" x="86" y="22">,</text><a xlink:href="../grammar_diagrams#index-column"><rect class="rule" x="35" y="35" width="106" height="25"/><text class="text" x="45" y="52">index_column</text></a><polygon points="172,59 176,59 176,45 172,45" style="fill:black;stroke-width:0"/></svg>

#### clustering_key_column_ordering

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="641" height="100" viewbox="0 0 641 100"><path class="connector" d="M0 52h15m54 0h10m99 0h10m63 0h10m35 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h98m24 0h98q5 0 5 5v20q0 5-5 5m-109 0h30m45 0h29m-84 25q0 5 5 5h5m54 0h5q5 0 5-5m-79-25q5 0 5 5v33q0 5 5 5h64q5 0 5-5v-33q0-5 5-5m5 0h30m25 0h15"/><polygon points="0,59 5,52 0,45" style="fill:black;stroke-width:0"/><rect class="literal" x="15" y="35" width="54" height="25" rx="7"/><text class="text" x="25" y="52">WITH</text><rect class="literal" x="79" y="35" width="99" height="25" rx="7"/><text class="text" x="89" y="52">CLUSTERING</text><rect class="literal" x="188" y="35" width="63" height="25" rx="7"/><text class="text" x="198" y="52">ORDER</text><rect class="literal" x="261" y="35" width="35" height="25" rx="7"/><text class="text" x="271" y="52">BY</text><rect class="literal" x="306" y="35" width="25" height="25" rx="7"/><text class="text" x="316" y="52">(</text><rect class="literal" x="454" y="5" width="24" height="25" rx="7"/><text class="text" x="464" y="22">,</text><a xlink:href="../grammar_diagrams#index-column"><rect class="rule" x="361" y="35" width="106" height="25"/><text class="text" x="371" y="52">index_column</text></a><rect class="literal" x="497" y="35" width="45" height="25" rx="7"/><text class="text" x="507" y="52">ASC</text><rect class="literal" x="497" y="65" width="54" height="25" rx="7"/><text class="text" x="507" y="82">DESC</text><rect class="literal" x="601" y="35" width="25" height="25" rx="7"/><text class="text" x="611" y="52">)</text><polygon points="637,59 641,59 641,45 637,45" style="fill:black;stroke-width:0"/></svg>

#### index_column

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="188" height="65" viewbox="0 0 188 65"><path class="connector" d="M0 22h35m107 0h31m-153 0q5 0 5 5v20q0 5 5 5h5m118 0h5q5 0 5-5v-20q0-5 5-5m5 0h15"/><polygon points="0,29 5,22 0,15" style="fill:black;stroke-width:0"/><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="35" y="5" width="107" height="25"/><text class="text" x="45" y="22">column_name</text></a><a xlink:href="../grammar_diagrams#jsonb-attribute"><rect class="rule" x="35" y="35" width="118" height="25"/><text class="text" x="45" y="52">jsonb_attribute</text></a><polygon points="184,29 188,29 188,15 184,15" style="fill:black;stroke-width:0"/></svg>

#### jsonb_attribute

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="582" height="65" viewbox="0 0 582 65"><path class="connector" d="M0 37h15m107 0h50m-5 0q-5 0-5-5v-17q0-5 5-5h178q5 0 5 5v17q0 5-5 5m-139 0h10m124 0h40m-243 0q5 0 5 5v8q0 5 5 5h218q5 0 5-5v-8q0-5 5-5m5 0h10m43 0h10m124 0h15"/><polygon points="0,44 5,37 0,30" style="fill:black;stroke-width:0"/><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="15" y="20" width="107" height="25"/><text class="text" x="25" y="37">column_name</text></a><rect class="literal" x="172" y="20" width="34" height="25" rx="7"/><text class="text" x="182" y="37">-&gt;</text><rect class="literal" x="216" y="20" width="124" height="25" rx="7"/><text class="text" x="226" y="37">&apos;attribute_name&apos;</text><rect class="literal" x="390" y="20" width="43" height="25" rx="7"/><text class="text" x="400" y="37">-&gt;&gt;</text><rect class="literal" x="443" y="20" width="124" height="25" rx="7"/><text class="text" x="453" y="37">&apos;attribute_name&apos;</text><polygon points="578,44 582,44 582,30 578,30" style="fill:black;stroke-width:0"/></svg>

#### covering_columns

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="383" height="95" viewbox="0 0 383 95"><path class="connector" d="M0 52h35m86 0h20m-121 0q5 0 5 5v20q0 5 5 5h5m76 0h15q5 0 5-5v-20q0-5 5-5m5 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h47q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h15"/><polygon points="0,59 5,52 0,45" style="fill:black;stroke-width:0"/><rect class="literal" x="35" y="35" width="86" height="25" rx="7"/><text class="text" x="45" y="52">COVERING</text><rect class="literal" x="35" y="65" width="76" height="25" rx="7"/><text class="text" x="45" y="82">INCLUDE</text><rect class="literal" x="151" y="35" width="25" height="25" rx="7"/><text class="text" x="161" y="52">(</text><rect class="literal" x="247" y="5" width="24" height="25" rx="7"/><text class="text" x="257" y="22">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="206" y="35" width="107" height="25"/><text class="text" x="216" y="52">column_name</text></a><rect class="literal" x="343" y="35" width="25" height="25" rx="7"/><text class="text" x="353" y="52">)</text><polygon points="379,59 383,59 383,45 379,45" style="fill:black;stroke-width:0"/></svg>

#### index_predicate

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="158" height="35" viewbox="0 0 158 35"><path class="connector" d="M0 22h15m128 0h15"/><polygon points="0,29 5,22 0,15" style="fill:black;stroke-width:0"/><a xlink:href="../grammar_diagrams#where-expression"><rect class="rule" x="15" y="5" width="128" height="25"/><text class="text" x="25" y="22">where_expression</text></a><polygon points="154,29 158,29 158,15 154,15" style="fill:black;stroke-width:0"/></svg>

### Grammar

```ebnf
create_index ::= CREATE INDEX [ IF NOT EXISTS ] index_name
                     ON table_name ( partition_key_columns [ clustering_key_columns ] )
                     [ clustering_key_column_ordering ] [ covering_columns ] [ 'WHERE' index_predicate ]

partition_key_columns ::= index_column | ( index_column [ , ... ] )

clustering_key_columns ::= index_column [ , ... ]

clustering_key_column_ordering ::= WITH CLUSTERING ORDER BY ( { index_column [ ASC | DESC ] } [ , ... ] )

index_column ::= column_name | jsonb_attribute

jsonb_attribute ::= column_name [ -> 'attribute_name' [ ... ] ] ->> 'attribute_name'

covering_columns ::= { COVERING | INCLUDE } ( column_name [ , ... ] )

index_predicate ::= where_expression
```

Where

- `index_name`, `table_name`, and `column_name` are identifiers. `table_name` may be qualified with a keyspace name. `index_name` cannot be qualified with a keyspace name because an index must be created in the table's keyspace.

## Semantics

- An error is raised if transactions have not be enabled using the `WITH transactions = { 'enabled' : true }` clause on the table to be indexed. This is because secondary indexes internally use distributed transactions to ensure ACID guarantees in the updates to the secondary index and the associated primary key. More details [here](https://blog.yugabyte.com/yugabyte-db-1-1-new-feature-speeding-up-queries-with-secondary-indexes/).
- An error is raised if `index_name` already exists in the associated keyspace unless the `IF NOT EXISTS` option is used.
- Indexes do not support TTL. An error is raised if data is inserted with TTL into a table with indexes.

{{< note title="Note" >}}

When an index is created on an existing table, YugabyteDB will automatically backfill existing data into the index in an online manner (that is, while continuing to serve other concurrent writes and traffic). For more details on how this is done, see [Online Index Backfill](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md).

{{< /note >}}

### PARTITION KEY

- Partition key is required and defines a split of the index into _partitions_.

### CLUSTERING KEY

- Clustering key is optional and defines an ordering for index rows within a partition.
- Default ordering is ascending (`ASC`) but can be set for each clustering column as ascending or descending using the `CLUSTERING ORDER BY` property.
- Any primary key column of the table not indexed explicitly in `index_columns` is added as a clustering column to the index implicitly. This is necessary so that the whole primary key of the table is indexed.

### INCLUDED COLUMNS

- Included columns are optional table columns whose values are copied into the index in addition to storing them in the table. When additional columns are included in the index, they can be used to respond to queries directly from the index without querying the table.

- The following can't be added to an index's included columns: static columns of a table, expressions, and table columns with the following types: frozen, map, set, list, tuple, jsonb, and user defined.

### UNIQUE INDEX

- A unique index disallows duplicate values from being inserted into the indexed columns. It can be used to ensure uniqueness of index column values.

### PARTIAL INDEX

- If a `WHERE` clause is specified, only rows which satisfy the `index_predicate` are indexed.
- An `index_predicate` can have sub-expressions on columns of these data types: `TINYINT`, `SMALLINT`, `INT/INTEGER`, `BIGINT`, `VARINT`, `BOOLEAN` and `TEXT` along with these operators (when applicable): `=, !=, >, <, >=, <=`.
- Partial indexes can be `UNIQUE`. A UNIQUE partial index enforces the constraint that for each possible tuple of indexed columns, only one row that satisfies the `index_predicate` is allowed in the table.
- `SELECT` queries can use a partial index for scanning if the `SELECT` statement's `where_expression` => (logically implies) `index_predicate`.

    {{< note title="Note" >}}

- A partial index might not be chosen even if the implication holds in case there are better query plans.
- The logical implication holds if all sub-expressions of the `index_predicate` are present as is in the `where_expression`. For example, assume `where_expression = A AND B AND C`, `index_predicate_1 = A AND B`, `index_predicate_2 = A AND B AND D`, `index_predicate_3 = A AND B AND C AND D`. Then `where_expression` only implies `index_predicate_1`

- Currently, valid mathematical implications are not taken into account when checking for logical implication. For example, even if `where_expression = x > 5` and `index_predicate = x > 4`, the `SELECT` query will not use the index for scanning. This is because the two sub-expressions `x > 5` and `x > 4` differ.

    {{< /note >}}

- When using a prepared statement, the logical implication check (to decide if a partial index is usable), will only consider those sub-expressions of `where_expression` that don't have a bind variable. This is because the query plan is decided before execution (i.e., when a statement is prepared).

```sql
ycqlsh:example> CREATE TABLE orders (customer_id INT,
                                    order_date TIMESTAMP,
                                    product JSONB,
                                    warehouse_id INT,
                                    amount DOUBLE,
                                    PRIMARY KEY ((customer_id), order_date))
                WITH transactions = { 'enabled' : true };

ycqlsh:example> CREATE INDEX idx ON orders (warehouse_id)
                WHERE warehouse_id < 100;

ycqlsh:example> EXPLAIN SELECT product FROM orders
                WHERE warehouse_id < 100 AND order_date >= ?; // Idx can be used
```

```output
 QUERY PLAN
------------------------------------------
 Index Scan using temp.idx on temp.orders
   Filter: (order_date >= :order_date)
```

```sql
ycqlsh:example> EXPLAIN SELECT product FROM orders
                WHERE warehouse_id < ? and order_date >= ?; // Idx cannot be used
```

```output
 QUERY PLAN
--------------------------------------------------------------------------
 Seq Scan on temp.orders
   Filter: (warehouse_id < :warehouse_id) AND (order_date >= :order_date)
```

- Without partial indexes, we do not allow many combinations of operators together on the same column in a `SELECT`'s where expression e.g.: `WHERE v1 != NULL and v1 = 5`. But if there was a partial index that subsumes some clauses of the `SELECT`'s where expression, two or more operators otherwise not supported together, might be supported.

```sql
ycqlsh:example> EXPLAIN SELECT product FROM orders
                WHERE warehouse_id != NULL AND warehouse_id = ?;
```

```output
SyntaxException: Invalid CQL Statement. Illogical condition for where clause
EXPLAIN SELECT product from orders where warehouse_id != NULL and warehouse_id = ?;
                                                                  ^^^^^^^^^^^^
 (ql error -12)
```

```sql
ycqlsh:example> CREATE INDEX warehouse_idx ON orders (warehouse_id)
                WHERE warehouse_id != NULL;
ycqlsh:example> EXPLAIN SELECT product FROM orders
                WHERE warehouse_id != NULL AND warehouse_id = ?; // warehouse_idx can be used
```

```output
 QUERY PLAN
----------------------------------------------------
 Index Scan using temp.warehouse_idx on temp.orders
   Key Conditions: (warehouse_id = :warehouse_id)
```

## Examples

### Create a table to be indexed

'customer_id' is the partitioning column and 'order_date' is the clustering column.

```sql
ycqlsh:example> CREATE TABLE orders (customer_id INT,
                                    order_date TIMESTAMP,
                                    product JSONB,
                                    warehouse_id INT,
                                    amount DOUBLE,
                                    PRIMARY KEY ((customer_id), order_date))
                WITH transactions = { 'enabled' : true };
```

### Create an index for query by the `order_date` column

```sql
ycqlsh:example> CREATE INDEX orders_by_date ON orders (order_date) INCLUDE (amount);
```

### Create an index for query by the JSONB attribute `product->>'name'`

```sql
ycqlsh:example> CREATE INDEX product_name
                ON orders (product->>'name') INCLUDE (amount);
```

### Create an index for query by the `warehouse_id` column

```sql
ycqlsh:example> CREATE INDEX orders_by_warehouse
                ON orders (warehouse_id, order_date) INCLUDE (amount);
```

### Insert some data

```sql
ycqlsh:example> INSERT INTO orders (customer_id, order_date, product, warehouse_id, amount)
                VALUES (1001, '2018-01-10', '{ "name":"desk" }', 107, 100.30);
ycqlsh:example> INSERT INTO orders (customer_id, order_date, product, warehouse_id, amount)
                VALUES (1002, '2018-01-11', '{ "name":"chair" }', 102, 50.45);
ycqlsh:example> INSERT INTO orders (customer_id, order_date, product, warehouse_id, amount)
                VALUES (1001, '2018-04-09', '{ "name":"pen" }', 102, 20.25);
ycqlsh:example> INSERT INTO orders (customer_id, order_date, product, warehouse_id, amount)
                VALUES (1003, '2018-04-09', '{ "name":"pencil" }', 108, 200.80);
```

### Query by the partition column `customer_id` in the table

```sql
ycqlsh:example> SELECT SUM(amount) FROM orders
                WHERE customer_id = 1001 AND order_date >= '2018-01-01';
```

```output
  sum(amount)
-------------
      120.55
```

### Query by the partition column `order_date` in the index `orders_by_date`

```sql
ycqlsh:example> SELECT SUM(amount) FROM orders
                WHERE order_date = '2018-04-09';
```

```output
 sum(amount)
-------------
      221.05
```

### Query by the partition column `product->>'name'` in the index `product_name`

```sql
ycqlsh:example> SELECT SUM(amount) FROM orders
                WHERE product->>'name' = 'desk';
```

```output
 sum(amount)
-------------
      100.30
```

### Query by the partition column `warehouse_id` column in the index `orders_by_warehouse`

```sql
ycqlsh:example> SELECT SUM(amount) FROM orders
                WHERE warehouse_id = 102 AND order_date >= '2018-01-01';
```

```output
 sum(amount)
-------------
        70.7
```

### Create a table with a unique index

You can do this as shown below.

```sql
ycqlsh:example> CREATE TABLE emp (enum INT primary key,
                                 lastname VARCHAR,
                                 firstname VARCHAR,
                                 userid VARCHAR)
               WITH transactions = { 'enabled' : true };
ycqlsh:example> CREATE UNIQUE INDEX emp_by_userid ON emp (userid);
```

### Insert values into the table and verify no duplicate `userid` is inserted

```sql
ycqlsh:example> INSERT INTO emp (enum, lastname, firstname, userid)
                VALUES (1001, 'Smith', 'John', 'jsmith');
ycqlsh:example> INSERT INTO emp (enum, lastname, firstname, userid)
                VALUES (1002, 'Smith', 'Jason', 'jsmith');
```

```output
InvalidRequest: Error from server: code=2200 [Invalid query] message="SQL error: Execution Error. Duplicate value disallowed by unique index emp_by_userid
INSERT INTO emp (enum, lastname, firstname, userid)
       ^^^^
VALUES (1002, 'Smith', 'Jason', 'jsmith');
 (error -300)"
```

```sql
ycqlsh:example> INSERT INTO emp (enum, lastname, firstname, userid)
                VALUES (1002, 'Smith', 'Jason', 'jasmith');
ycqlsh:example> SELECT * FROM emp;
```

```output
 enum | lastname | firstname | userid
------+----------+-----------+---------
 1002 |    Smith |     Jason | jasmith
 1001 |    Smith |      John |  jsmith
```

## See also

- [`CREATE TABLE`](../ddl_create_table)
- [`DROP INDEX`](../ddl_drop_index)
