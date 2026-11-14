PRAGMA foreign_keys = ON;
CREATE TABLE licenses (
 id TEXT PRIMARY KEY,
 code_verifier TEXT NOT NULL UNIQUE,
 status TEXT NOT NULL DEFAULT 'active' CHECK(status IN ('active','revoked')),
 max_activations INTEGER NOT NULL DEFAULT 5 CHECK(max_activations BETWEEN 1 AND 5),
 issued_at INTEGER NOT NULL,
 revoked_at INTEGER,
 revocation_reason TEXT
);
CREATE TABLE installations (
 license_id TEXT NOT NULL REFERENCES licenses(id),
 installation_id TEXT NOT NULL,
 public_key TEXT NOT NULL,
 label TEXT NOT NULL,
 activated_at INTEGER NOT NULL,
 verified_at INTEGER NOT NULL,
 deactivated_at INTEGER,
 PRIMARY KEY(license_id, installation_id)
);
CREATE INDEX active_installations ON installations(license_id, deactivated_at);
-- Enforce the count inside the write transaction, including reactivation.
CREATE TRIGGER activation_insert BEFORE INSERT ON installations
 WHEN NEW.deactivated_at IS NULL
 BEGIN
 SELECT CASE WHEN NOT EXISTS(SELECT 1 FROM licenses WHERE id=NEW.license_id AND status='active') THEN RAISE(ABORT,'license_revoked') END;
 SELECT CASE WHEN (SELECT count(*) FROM installations WHERE license_id=NEW.license_id AND deactivated_at IS NULL) >= (SELECT max_activations FROM licenses WHERE id=NEW.license_id) THEN RAISE(ABORT,'activation_limit') END;
 END;
CREATE TRIGGER activation_update BEFORE UPDATE OF deactivated_at ON installations
 WHEN OLD.deactivated_at IS NOT NULL AND NEW.deactivated_at IS NULL
 BEGIN
 SELECT CASE WHEN NOT EXISTS(SELECT 1 FROM licenses WHERE id=NEW.license_id AND status='active') THEN RAISE(ABORT,'license_revoked') END;
 SELECT CASE WHEN (SELECT count(*) FROM installations WHERE license_id=NEW.license_id AND deactivated_at IS NULL) >= (SELECT max_activations FROM licenses WHERE id=NEW.license_id) THEN RAISE(ABORT,'activation_limit') END;
 END;
CREATE TABLE request_nonces (actor TEXT NOT NULL, nonce TEXT NOT NULL, expires_at INTEGER NOT NULL, PRIMARY KEY(actor,nonce));

CREATE INDEX nonce_expiry ON request_nonces(expires_at);
