# Security Notes

BadgeOps is a teaching scaffold. It is useful for demos, attendance, event check-in, and internal prototypes. It is not a secure physical access-control system by default.

Required warning:

"UID-only RFID/NFC access is suitable for demos, attendance tracking, event check-in, prototypes, and low-risk internal tools. It should not be treated as secure access control. Many low-cost RFID/NFC cards and tags can be cloned. For serious access control, use stronger credential design, signed tokens, backend validation, secure elements, audit logging, and proper threat modeling."

## Practical Guidance

- Never present UID-only checks as strong authentication.
- Do not put secrets in Serial logs.
- Treat mock badges and sample data as fake data.
- Add backend validation before serious check-in workflows.
- Add audit logs before allowing any real-world access decision.
