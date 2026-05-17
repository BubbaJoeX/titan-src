-- Open-world claim system persistence (optional; runtime state is authoritative on game server).
WHENEVER SQLERROR EXIT SQL.SQLCODE
WHENEVER OSERROR EXIT FAILURE

declare
  cnt number;
begin
  select count(*) into cnt from user_tables where table_name = 'CLAIMS';
  if (cnt = 0) then
    execute immediate '
      create table claims
      (
        claim_id number(10) not null,
        account_id number(20) not null,
        owner_character_oid number(20) not null,
        marker_oid number(20) not null,
        terminal_oid number(20),
        scene_id varchar2(256) not null,
        center_x number not null,
        center_y number not null,
        center_z number not null,
        radius_m number not null,
        status number(5) default 0 not null,
        next_maintenance_at date,
        created_at date default sysdate,
        primary key (claim_id)
      )';
    execute immediate 'create index claims_account_idx on claims (account_id)';
    execute immediate 'create index claims_scene_idx on claims (scene_id)';
    execute immediate 'grant select on claims to public';
  end if;
end;
/

declare
  cnt number;
begin
  select count(*) into cnt from user_tables where table_name = 'CLAIM_BANS';
  if (cnt = 0) then
    execute immediate '
      create table claim_bans
      (
        claim_id number(10) not null,
        banned_character_oid number(20) not null,
        created_at date default sysdate,
        expires_at date,
        primary key (claim_id, banned_character_oid)
      )';
    execute immediate 'create index claim_bans_claim_idx on claim_bans (claim_id)';
    execute immediate 'grant select on claim_bans to public';
  end if;
end;
/

declare
  cnt number;
begin
  select count(*) into cnt from user_tables where table_name = 'CLAIM_TAX_BALANCE';
  if (cnt = 0) then
    execute immediate '
      create table claim_tax_balance
      (
        claim_id number(10) not null,
        resource_key varchar2(128) not null,
        quantity number(20) default 0 not null,
        last_updated date default sysdate,
        primary key (claim_id, resource_key)
      )';
    execute immediate 'create index claim_tax_claim_idx on claim_tax_balance (claim_id)';
    execute immediate 'grant select on claim_tax_balance to public';
  end if;
end;
/

-- Idempotent version bump (safe if re-run after a partial apply).
update version_number set version_number = GREATEST(version_number, 273), min_version_number = GREATEST(min_version_number, 273);

exit;
