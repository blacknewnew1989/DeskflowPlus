# COMP-001 Runtime Composition Audit

Audit baseline: `a84e751db1eea9b10983dde50bedd9267fbc380e` (`origin/product/relaydesk-v1` on 2026-08-13).

## Status language

- `IMPLEMENTED`: production code exists. This does not imply that the GUI creates or uses it.
- `WIRED`: the production GUI startup path owns the component and connects its inputs and outputs to the next runtime component.
- `NOT_WIRED`: production code exists, but no path reachable from `MainWindow` constructs or connects it.
- `NOT_RUN`: the stated runtime behavior was not executed on real peers/platforms during this audit. A unit test is never used as evidence for runtime wiring.

The requested reference `product/docs/18_IMPLEMENTATION_TASKS.md` is absent from the baseline, its remote tree, and path history. The available numbered document is `product/docs/18_SHARED_CONTRACTS.md`; task acceptance was therefore checked against `01_PRD.md`, `16_BACKLOG.md`, `18_SHARED_CONTRACTS.md`, and the production source tree.

## Startup boundary

The GUI executable constructs `MainWindow`. `MainWindow.cpp` constructs `DeviceHomeModel`, a standalone `PairingStateMachine`/`PairingWizardModel`, `PermissionStatusModel`, `DevicesDock`, `TransferCenterModel`, and `TransferCenterDock`. The docks consume their corresponding models and are placed in the real window, so this UI-only segment is `WIRED`.

No production reference outside each component's own implementation/CMake target constructs `DeviceIdentity`, `DiscoveryService`, `DiscoveryRegistry`, `PairingService`, `TrustedDeviceStore`, `AutoReconnectCoordinator`, `FileTlsListener`, or `FileTlsClient`. The file transfer worker/state/store types likewise have no application service owner.

## Composition findings

| Runtime path | Production implementation | Startup/runtime composition | Status | Evidence and consequence |
|---|---|---|---|---|
| GUI startup -> Devices Dock | `DeviceHomeModel`, `PairingWizardModel`, `PermissionStatusModel`, `DevicesDock` | `MainWindow` owns all four and passes the model references to the dock | `WIRED` | `src/lib/gui/MainWindow.cpp`; the dock is real, but the device model begins empty and has no service producer |
| `DeviceIdentity` -> local identity | Stable persisted ID helper exists | No startup owner calls `loadOrCreate`; its key is also absent from the upstream `Settings` valid-key allowlist | `NOT_WIRED` | `src/lib/relaydesk/device/DeviceIdentity.*`, `src/lib/common/Settings.h` |
| discovery socket -> registry | `DiscoveryService` performs bounded UDP receive/advertise; `DiscoveryRegistry` deduplicates and expires peers | Neither object is constructed, and `advertisementReceived` has no production connection to `observeAdvertisement` | `NOT_WIRED` | `src/lib/relaydesk/discovery/DiscoveryService.*`, `DiscoveryRegistry.*` |
| registry -> device model -> Devices Dock | Registry emits immutable `DeviceSnapshot`; model has `upsertRemoteDevice`; Dock observes the model | Model -> Dock is wired; registry -> model is absent | `NOT_WIRED` | `MainWindow.cpp`, `DiscoveryRegistry.h`, `DeviceHomeModel.h` |
| pairing UI intent -> pairing service -> trust | `PairingService`/`PairingManager` and `TrustedDeviceStore` exist | MainWindow creates a separate state machine only. `DevicesDock::pairingRequested` has no production receiver; no service/store is owned | `NOT_WIRED` | `src/lib/relaydesk/pairing`, `src/lib/relaydesk/trust`, `DevicesDock.h`, `MainWindow.cpp` |
| trust/discovery -> automatic reconnect | `AutoReconnectCoordinator` and address selection exist | No production owner supplies discovery/trust snapshots or executes connect attempts | `NOT_WIRED` | `src/lib/relaydesk/reconnect` |
| trust -> dedicated file TLS listener/client | Pinned `FileTlsListener`, `FileTlsClient`, and framed connection exist | No startup listener/client, certificate/store adapter, or accepted-connection owner exists | `NOT_WIRED` | `src/lib/relaydesk/filetransport/FileTlsTransport.*` |
| send intent -> manifest/offer/sender -> TLS sink | Manifest builders, offer codec/state, `TransferSender`, and TLS frame sink exist | `DevicesDock::sendItemsRequested` has no production receiver; no transfer service composes the workers | `NOT_WIRED` | `src/lib/relaydesk/transfer`, `src/lib/relaydesk/filetransport`, `MainWindow.cpp` |
| incoming TLS frames -> offer/receiver/resume | Offer state/model, `FileReceiver`, `ResumeStore`, durable checkpoints, and resume codecs exist | `IncomingOfferModel` is not instantiated or attached to the Dock; listener frames have no application dispatcher | `NOT_WIRED` | `IncomingOfferModel.*`, `DevicesDock::setIncomingOfferModel`, transfer/filetransport sources |
| pause/resume/cancel UI -> transfer control | `TransferControlStateMachine` and sender pump control exist | Transfer Center emits pause/resume/cancel intents, but MainWindow connects only terminal notifications; no service consumes control intents | `NOT_WIRED` | `TransferCenterModel.h`, `MainWindow.cpp` |
| transfer progress/history -> Transfer Center | Publisher/estimator and bounded history store exist; model/dock display snapshots/history | No service publishes snapshots into the model and no production owner loads/appends history | `NOT_WIRED` | `TransferProgressPublisher.*`, `TransferHistoryStore.*`, `TransferCenterModel.*` |

## Runtime validation

| Scenario | Result | Reason |
|---|---|---|
| Two real GUI processes discover one another and update Devices Dock | `NOT_RUN` | The audited baseline has no discovery startup composition, so a unit-level UDP or model test cannot establish this behavior |
| Pairing commits trust and changes the device card | `NOT_RUN` | Pairing UI and service/store are disconnected |
| Trusted peer establishes the independent file TLS channel | `NOT_RUN` | Listener/client are not owned by the application |
| Offer through transfer completion, resume, control, and history UI | `NOT_RUN` | There is no application-level file transfer service/dispatcher |
| Windows <-> macOS end-to-end product path | `NOT_RUN` | Requires the missing composition plus two real packaged peers |

## Minimal next composition slice

Create one GUI-lifetime discovery composition that:

1. loads/creates the stable local `DeviceId` in the active Deskflow settings file;
2. constructs one `DiscoveryService` and one `DiscoveryRegistry` on the GUI thread;
3. connects service advertisements to the registry and registry snapshots to the existing `DeviceHomeModel` used by `DevicesDock`;
4. starts only when discovery is enabled, logs typed diagnostics, and stops before window-owned network objects are destroyed;
5. adds a deterministic composition test for snapshot flow and ownership, while keeping real two-peer discovery marked `NOT_RUN` until it is actually executed.

This slice does not imply pairing, reconnect, trust, or transfer runtime completion.
