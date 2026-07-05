# Mock API

Small Express API for local tutorials and Serial-to-HTTP experiments.

## Run

```sh
cd mock-api
npm install
npm start
```

Default port: `8787`

Use a temporary database for tests:

```sh
DB_PATH=/tmp/crowpanel-aiot-db.json npm start
```

## Endpoints

- `GET /health`
- `POST /events`
- `GET /events`
- `POST /summary`
- `GET /badges`
- `POST /badges`
- `POST /inspection`

The Arduino sketches do not require this server in mock mode. It is here for later Wi-Fi/API demos.
