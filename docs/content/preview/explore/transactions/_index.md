---
title: Transactions
headerTitle: Transactions
linkTitle: Transactions
description: Transactions in YugabyteDB.
headcontent: Transactions in YugabyteDB.
image: /images/section_icons/explore/transactional.png
menu:
  preview:
    identifier: explore-transactions
    parent: explore
    weight: 240
type: indexpage
---
YugabyteDB is a transactional database that supports distributed transactions. A transaction is a sequence of operations performed as a single logical unit of work. A transaction has four key properties - **Atomicity**, **Consistency**, **Isolation** and **Durability** - commonly abbreviated as ACID.

The following table summarizes the support for transactions across the YSQL and YCQL APIs.

| Property | YSQL | YCQL | Comments |
| :------- | :--- | :--- | :------- |
| [Distributed transactions](distributed-transactions-ysql/) | Yes | Yes | Perform multi-row or multi-table transactions.<br/>Application can connect to any node of the cluster. |
| [Isolation levels](isolation-levels/) | Serializable<br/>Snapshot | Snapshot | Repeatable read isolation level in PostgreSQL maps to snapshot isolation in YSQL. |
| Set `AUTOCOMMIT = false` | Yes | No | The transaction must be expressed as one statement in YCQL. |

<!--
| [Explicit locking](explicit-locking/) | Yes | No | Ability to perform row- and table-level locking |
| [DDL statements](ddl-operations/) | Transaction per DDL statement  | Transaction per DDL statement | Each DDL statement is a transaction in both YSQL and YCQL, even if other DDL statements are in a transaction block in YSQL. |
| [Non-transactional tables](non-transactional-tables/) | No | Yes | Ability to disable multi-row transactions on a per-table basis. <br/>Useful for some features such as automatic data expiry. |
-->

The various features are explained in the following sections.

<div class="row">

   <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="distributed-transactions-ysql/">
      <div class="head">
        <div class="icon"><i class="fa-solid fa-sitemap"></i></div>
        <div class="title">Distributed transactions</div>
      </div>
      <div class="body">
        Understand how distributed transactions work in YugabyteDB.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="isolation-levels/">
      <div class="head">
        <div class="icon"><i class="fa-solid fa-bars-staggered"></i></div>
        <div class="title">Isolation levels</div>
      </div>
      <div class="body">
        Serializable, Snapshot, and Read committed isolation in YugabyteDB.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="explicit-locking/">
      <div class="head">
        <div class="icon"><i class="fa-solid fa-lock"></i></div>
        <div class="title">Explicit locking</div>
      </div>
      <div class="body">
        Explicit row-level locking in YSQL.
      </div>
    </a>
  </div>
<!-- ADD THIS ONCE READY:
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="ddl-operations/">
      <div class="head">
        <div class="icon"><i class="fa-solid fa-table"></i></div>
        <div class="title">DDL Operations</div>
      </div>
      <div class="body">
        How YugabyteDB handles DDL operations in transaction blocks.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="non-transactional-tables/">
      <div class="head">
        <div class="icon"><i class="fa-solid fa-strikethrough"></i></div>
        <div class="title">Non-Transactional Tables</div>
      </div>
      <div class="body">
        Disable multi-row transactions on a per-table basis in YCQL.
      </div>
    </a>
  </div>
-->
</div>
