-- Allow atomic UPSERT of the same active installation at the five-device cap.
DROP TRIGGER activation_insert;
CREATE TRIGGER activation_insert BEFORE INSERT ON installations
 WHEN NEW.deactivated_at IS NULL AND NOT EXISTS (SELECT 1 FROM installations WHERE license_id=NEW.license_id AND installation_id=NEW.installation_id AND deactivated_at IS NULL)
 BEGIN
 SELECT CASE WHEN NOT EXISTS(SELECT 1 FROM licenses WHERE id=NEW.license_id AND status='active') THEN RAISE(ABORT,'license_revoked') END;
 SELECT CASE WHEN (SELECT count(*) FROM installations WHERE license_id=NEW.license_id AND deactivated_at IS NULL) >= (SELECT max_activations FROM licenses WHERE id=NEW.license_id) THEN RAISE(ABORT,'activation_limit') END;
 END;
