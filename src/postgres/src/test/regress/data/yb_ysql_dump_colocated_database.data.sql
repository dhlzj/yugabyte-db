--
-- YSQL database dump
--

-- Dumped from database version 11.2-YB-2.17.1.0-b0
-- Dumped by ysql_dump version 11.2-YB-2.17.1.0-b0

SET yb_binary_restore = true;
SET yb_non_ddl_txn_for_sys_tables_allowed = true;
SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET client_min_messages = warning;
SET row_security = off;

SET default_tablespace = '';

SET default_with_oids = false;

--
-- Name: tbl; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16388'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16387'::pg_catalog.oid);


-- For YB colocation backup, must preserve implicit tablegroup pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16389'::pg_catalog.oid);
CREATE TABLE public.tbl (
    k integer NOT NULL,
    v integer,
    CONSTRAINT tbl_pkey PRIMARY KEY(k ASC)
)
WITH (colocation_id='20001');


ALTER TABLE public.tbl OWNER TO yugabyte_test;

--
-- Name: tbl2; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16394'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16393'::pg_catalog.oid);

CREATE TABLE public.tbl2 (
    k integer NOT NULL,
    v integer,
    v2 text,
    CONSTRAINT tbl2_pkey PRIMARY KEY(k ASC)
)
WITH (colocation_id='20002');


ALTER TABLE public.tbl2 OWNER TO yugabyte_test;

--
-- Name: tbl3; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16401'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16400'::pg_catalog.oid);

CREATE TABLE public.tbl3 (
    k integer NOT NULL,
    v integer,
    CONSTRAINT tbl3_pkey PRIMARY KEY((k) HASH)
)
WITH (colocation='false')
SPLIT INTO 3 TABLETS;


ALTER TABLE public.tbl3 OWNER TO yugabyte_test;

--
-- Name: tbl4; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16407'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16406'::pg_catalog.oid);

CREATE TABLE public.tbl4 (
    k integer NOT NULL,
    v integer,
    v2 text,
    CONSTRAINT tbl4_pkey PRIMARY KEY((k) HASH)
)
WITH (colocation='false')
SPLIT INTO 3 TABLETS;


ALTER TABLE public.tbl4 OWNER TO yugabyte_test;

--
-- Data for Name: tbl; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl (k, v) FROM stdin;
\.


--
-- Data for Name: tbl2; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl2 (k, v, v2) FROM stdin;
\.


--
-- Data for Name: tbl3; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl3 (k, v) FROM stdin;
\.


--
-- Data for Name: tbl4; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl4 (k, v, v2) FROM stdin;
\.


--
-- Name: tbl2_v2_idx; Type: INDEX; Schema: public; Owner: yugabyte_test
--

CREATE INDEX tbl2_v2_idx ON public.tbl2 USING lsm (v2 ASC) WITH (colocation_id=20004);


--
-- Name: tbl3_v_idx; Type: INDEX; Schema: public; Owner: yugabyte_test
--

CREATE UNIQUE INDEX tbl3_v_idx ON public.tbl3 USING lsm (v HASH) SPLIT INTO 3 TABLETS;


--
-- Name: tbl_v_idx; Type: INDEX; Schema: public; Owner: yugabyte_test
--

CREATE UNIQUE INDEX tbl_v_idx ON public.tbl USING lsm (v DESC) WITH (colocation_id=20003);


--
-- Name: FUNCTION pg_stat_statements_reset(); Type: ACL; Schema: pg_catalog; Owner: postgres
--

SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
REVOKE ALL ON FUNCTION pg_catalog.pg_stat_statements_reset() FROM PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);


--
-- Name: TABLE pg_stat_statements; Type: ACL; Schema: pg_catalog; Owner: postgres
--

SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
GRANT SELECT ON TABLE pg_catalog.pg_stat_statements TO PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);


--
-- YSQL database dump complete
--

