---
title: SHOW TRANSACTION statement [YSQL]
headerTitle: SHOW TRANSACTION
linkTitle: SHOW TRANSACTION
description: Use the SHOW TRANSACTION statement to show the current transaction isolation level.
summary: SHOW TRANSACTION
menu:
  v2.6:
    identifier: txn_show
    parent: statements
type: docs
---

## Synopsis

Use the `SHOW TRANSACTION` statement to show the current transaction isolation level.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/show_transaction.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/show_transaction.diagram.md" %}}
  </div>
</div>

## Semantics

Supports both Serializable and Snapshot Isolation using the PostgreSQL isolation level syntax of `SERIALIZABLE` and `REPEATABLE READ` respectively. Even `READ COMMITTED` and `READ UNCOMMITTED` isolation levels are mapped to Snapshot Isolation.

### TRANSACTION ISOLATION LEVEL

Show the current transaction isolation level.

The `TRANSACTION ISOLATION LEVEL` returned is either `SERIALIZABLE` or `REPEATABLE READ`. In YugabyteDB, the `READ COMMITTED` and `READ UNCOMMITTED` of PostgreSQL are mapped to `REPEATABLE READ`.

## See also

- [`SET TRANSACTION`](../txn_set)
- [`Transaction isolation levels`](../../../../../architecture/transactions/isolation-levels)
