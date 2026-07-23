-- Recover restart-persistent Tatooine filler POI duplicates.
--
-- Usage (SQL*Plus, connected as the game schema):
--   @recover_duplicate_city_poi.sql 0 0 0
--       Dry run.  This is always the first invocation.
--
--   @recover_duplicate_city_poi.sql 1 <reported duplicate count> <reported slot count>
--       Mark only the exact dry-run set as Replaced (delete reason 12).
--
-- The apply mode intentionally requires both dry-run counts.  A changed object
-- set aborts the transaction instead of applying a stale recovery decision.
-- Normal DatabaseServer lazy deletion can purge the marked rows later.

whenever sqlerror exit failure rollback
set verify off
set serveroutput on
set pagesize 500
set linesize 240
set trimspool on

define apply_mode = '&1'
define expected_duplicate_count = '&2'
define expected_slot_count = '&3'

column template_name format a78

prompt
prompt === Tatooine city POI recovery dry run ===

with candidates as
(
	select o.object_id,
		o.x,
		o.z,
		t.name template_name,
		case when exists
		(
			select 1
			from object_variables_view v
			where v.object_id = o.object_id
			and v.name = 'fillerSpawn.owner'
		) then 1 else 0 end has_owner_marker
	from objects o
	join object_templates t on t.id = o.object_template_id
	where o.deleted = 0
	and o.player_controlled = 'N'
	and o.contained_by = 0
	and o.scene_id = 'tatooine'
	and o.type_id = 1413566031
	and t.name in
	(
		'object/tangible/poi/tatooine/poi_city_convo.iff',
		'object/tangible/poi/tatooine/poi_city_droid_convo.iff',
		'object/tangible/poi/tatooine/poi_city_droid_convo2.iff',
		'object/tangible/poi/tatooine/poi_city_jawa_convo.iff',
		'object/tangible/poi/tatooine/poi_city_street_music.iff'
	)
	and not exists
	(
		select 1
		from player_objects p
		where p.object_id = o.object_id
	)
),
ranked as
(
	select candidates.*,
		row_number() over
		(
			partition by x, z
			order by has_owner_marker desc, object_id
		) slot_rank
	from candidates
)
select count(*) candidate_count,
	count(case when slot_rank = 1 then 1 end) retained_slot_count,
	count(case when slot_rank > 1 then 1 end) duplicate_count,
	min(object_id) oldest_object_id,
	max(object_id) newest_object_id
from ranked;

prompt
prompt === Duplicate counts by template ===

with candidates as
(
	select o.object_id,
		o.x,
		o.z,
		t.name template_name,
		case when exists
		(
			select 1
			from object_variables_view v
			where v.object_id = o.object_id
			and v.name = 'fillerSpawn.owner'
		) then 1 else 0 end has_owner_marker
	from objects o
	join object_templates t on t.id = o.object_template_id
	where o.deleted = 0
	and o.player_controlled = 'N'
	and o.contained_by = 0
	and o.scene_id = 'tatooine'
	and o.type_id = 1413566031
	and t.name in
	(
		'object/tangible/poi/tatooine/poi_city_convo.iff',
		'object/tangible/poi/tatooine/poi_city_droid_convo.iff',
		'object/tangible/poi/tatooine/poi_city_droid_convo2.iff',
		'object/tangible/poi/tatooine/poi_city_jawa_convo.iff',
		'object/tangible/poi/tatooine/poi_city_street_music.iff'
	)
	and not exists
	(
		select 1
		from player_objects p
		where p.object_id = o.object_id
	)
),
ranked as
(
	select candidates.*,
		row_number() over
		(
			partition by x, z
			order by has_owner_marker desc, object_id
		) slot_rank
	from candidates
)
select template_name,
	count(*) duplicate_count
from ranked
where slot_rank > 1
group by template_name
order by template_name;

declare
	v_apply_mode number := to_number('&&apply_mode');
	v_expected_duplicate_count number := to_number('&&expected_duplicate_count');
	v_expected_slot_count number := to_number('&&expected_slot_count');
	v_candidate_count number;
	v_duplicate_count number;
	v_slot_count number;
	v_active_linked_children number;
begin
	select count(*),
		count(case when slot_rank > 1 then 1 end),
		count(case when slot_rank = 1 then 1 end)
	into v_candidate_count, v_duplicate_count, v_slot_count
	from
	(
		select o.object_id,
			row_number() over
			(
				partition by o.x, o.z
				order by
					case when exists
					(
						select 1
						from object_variables_view v
						where v.object_id = o.object_id
						and v.name = 'fillerSpawn.owner'
					) then 0 else 1 end,
					o.object_id
			) slot_rank
		from objects o
		join object_templates t on t.id = o.object_template_id
		where o.deleted = 0
		and o.player_controlled = 'N'
		and o.contained_by = 0
		and o.scene_id = 'tatooine'
		and o.type_id = 1413566031
		and t.name in
		(
			'object/tangible/poi/tatooine/poi_city_convo.iff',
			'object/tangible/poi/tatooine/poi_city_droid_convo.iff',
			'object/tangible/poi/tatooine/poi_city_droid_convo2.iff',
			'object/tangible/poi/tatooine/poi_city_jawa_convo.iff',
			'object/tangible/poi/tatooine/poi_city_street_music.iff'
		)
		and not exists
		(
			select 1
			from player_objects p
			where p.object_id = o.object_id
		)
	);

	select count(*)
	into v_active_linked_children
	from objects child
	where child.deleted = 0
	and child.object_id in
	(
		select to_number(v.value)
		from object_variables_view v
		where v.name in
		(
			'guy1', 'guy2', 'guy3', 'guy4',
			'jawa1', 'jawa2', 'jawa3',
			'droidSpawn', 'commoner1', 'commoner2', 'musician'
		)
		and validate_conversion(v.value as number) = 1
		and v.object_id in
		(
			select o.object_id
			from objects o
			join object_templates t on t.id = o.object_template_id
			where o.deleted = 0
			and o.player_controlled = 'N'
			and o.contained_by = 0
			and o.scene_id = 'tatooine'
			and o.type_id = 1413566031
			and t.name in
			(
				'object/tangible/poi/tatooine/poi_city_convo.iff',
				'object/tangible/poi/tatooine/poi_city_droid_convo.iff',
				'object/tangible/poi/tatooine/poi_city_droid_convo2.iff',
				'object/tangible/poi/tatooine/poi_city_jawa_convo.iff',
				'object/tangible/poi/tatooine/poi_city_street_music.iff'
			)
		)
	);

	dbms_output.put_line('Candidate parents: ' || v_candidate_count);
	dbms_output.put_line('Canonical slots retained: ' || v_slot_count);
	dbms_output.put_line('Duplicate parents selected: ' || v_duplicate_count);
	dbms_output.put_line('Active linked children: ' || v_active_linked_children);

	if v_apply_mode = 0 then
		dbms_output.put_line('DRY RUN ONLY: no rows changed.');
		rollback;
		return;
	end if;

	if v_apply_mode != 1 then
		raise_application_error(-20001, 'apply_mode must be 0 or 1');
	end if;

	if v_duplicate_count != v_expected_duplicate_count
		or v_slot_count != v_expected_slot_count then
		raise_application_error(-20002,
			'Dry-run counts changed; run mode 0 again before applying');
	end if;

	if v_duplicate_count = 0 then
		raise_application_error(-20003, 'No duplicate parents selected');
	end if;

	if v_active_linked_children != 0 then
		raise_application_error(-20004,
			'Active linked children exist; refusing parent-only recovery');
	end if;

	update objects target
	set target.deleted = 12,
		target.deleted_date = sysdate,
		target.load_with = null
	where target.object_id in
	(
		select object_id
		from
		(
			select o.object_id,
				row_number() over
				(
					partition by o.x, o.z
					order by
						case when exists
						(
							select 1
							from object_variables_view v
							where v.object_id = o.object_id
							and v.name = 'fillerSpawn.owner'
						) then 0 else 1 end,
						o.object_id
				) slot_rank
			from objects o
			join object_templates t on t.id = o.object_template_id
			where o.deleted = 0
			and o.player_controlled = 'N'
			and o.contained_by = 0
			and o.scene_id = 'tatooine'
			and o.type_id = 1413566031
			and t.name in
			(
				'object/tangible/poi/tatooine/poi_city_convo.iff',
				'object/tangible/poi/tatooine/poi_city_droid_convo.iff',
				'object/tangible/poi/tatooine/poi_city_droid_convo2.iff',
				'object/tangible/poi/tatooine/poi_city_jawa_convo.iff',
				'object/tangible/poi/tatooine/poi_city_street_music.iff'
			)
			and not exists
			(
				select 1
				from player_objects p
				where p.object_id = o.object_id
			)
		)
		where slot_rank > 1
	);

	if sql%rowcount != v_duplicate_count then
		raise_application_error(-20005,
			'Updated row count did not match validated duplicate count');
	end if;

	commit;
	dbms_output.put_line('APPLIED: marked ' || v_duplicate_count ||
		' duplicate city POI parents as Replaced.');
end;
/

undefine apply_mode
undefine expected_duplicate_count
undefine expected_slot_count
set verify on
exit
