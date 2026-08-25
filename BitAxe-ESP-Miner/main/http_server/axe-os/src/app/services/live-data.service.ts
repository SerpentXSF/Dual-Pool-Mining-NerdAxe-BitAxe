import { Injectable } from '@angular/core';
import { BehaviorSubject, Observable, Subject, EMPTY, timer, merge, fromEvent } from 'rxjs';
import { catchError, retry, share, tap, switchMap, startWith, scan, shareReplay, map, timeout, bufferTime, filter, distinctUntilChanged } from 'rxjs/operators';
import { webSocket, WebSocketSubject } from 'rxjs/webSocket';
import { SystemInfo as ISystemInfo } from 'src/app/generated/models';
import { SystemApiService } from './system.service';
import { environment } from 'src/environments/environment';

// Poll cadence and the freshness window that decides whether the socket is
// actually feeding us.
//
// The worst case is a tick landing just inside the freshness window (so it
// skips) followed by the next tick: the largest gap between payloads is
// therefore VISIBLE_POLL_MS + DATA_FRESH_MS + one round-trip. These values keep
// that at ~4.2s, comfortably under the 5000 ms staleness threshold that raises
// "Unable to reach the device" (home.component.ts::checkStaleData), so no phase
// alignment can flash the banner while HTTP is answering.
//
// DATA_FRESH_MS also has a floor: the firmware pushes a diff roughly every
// 500 ms and at least once a second (uptimeSeconds always changes), so anything
// comfortably above ~1s avoids redundant polls while the socket is healthy.
const VISIBLE_POLL_MS = 2500;
const HIDDEN_POLL_MS = 60000;
const DATA_FRESH_MS = 1500;

@Injectable({
  providedIn: 'root'
})
export class LiveDataService {
  // Wall-clock of the last payload actually received, from ANY source.
  private lastDataTime = 0;
  private socket$: WebSocketSubject<any> | null = null;
  private updates$ = new Subject<Partial<ISystemInfo>>();
  
  // Shared info stream for the whole app
  public readonly info$: Observable<ISystemInfo>;
  
  // Connection status for the UI
  private connectedSubject = new BehaviorSubject<boolean>(false);
  public connected$ = this.connectedSubject.asObservable();

  constructor(
    private systemService: SystemApiService
  ) {
    // Visibility stream for polling adjustments
    const visibility$ = fromEvent(document, 'visibilitychange').pipe(
      map(() => document.visibilityState),
      startWith(document.visibilityState),
      distinctUntilChanged(),
      shareReplay(1)
    );

    // Periodic polling fallback. The gate is whether DATA is actually arriving,
    // NOT whether the socket happens to be open: an open-but-silent WebSocket
    // used to disable polling entirely, so the dashboard went blind and reported
    // the device unreachable while HTTP was perfectly healthy. When the socket is
    // genuinely streaming, every tick sees fresh data and skips the request, so
    // this costs nothing in the healthy case.
    const fallbackPolling$ = visibility$.pipe(
      switchMap(state => {
        const interval = state === 'visible' ? VISIBLE_POLL_MS : HIDDEN_POLL_MS;
        return timer(interval, interval).pipe(
          switchMap(() => {
            const dataIsFresh = Date.now() - this.lastDataTime < DATA_FRESH_MS;
            if (this.connectedSubject.value && state === 'visible' && dataIsFresh) return EMPTY;
            return this.systemService.getInfo();
          })
        );
      }),
      catchError(() => EMPTY)
    );

    const updates$ = merge(
      this.connect().pipe(switchMap(() => EMPTY), catchError(() => EMPTY)),
      this.updates$.pipe(
        // Buffer updates to handle bursts when tab is resumed
        bufferTime(500),
        filter(msgs => msgs.length > 0),
        map(msgs => msgs.reduce((acc, curr) => ({ ...acc, ...curr }), {} as Partial<ISystemInfo>))
      ),
      fallbackPolling$
    );

    const initialInfo$ = this.systemService.getInfo().pipe(
      catchError(err => {
        console.error('Initial info fetch failed', err);
        return EMPTY;
      })
    );

    this.info$ = merge(initialInfo$, updates$).pipe(
      // Stamp freshness for the polling gate above, whatever the source was.
      tap(() => this.lastDataTime = Date.now()),
      scan((acc: ISystemInfo, curr: Partial<ISystemInfo>) => ({ ...acc, ...curr } as ISystemInfo), {} as ISystemInfo),
      // Ensure we have at least once received a message with a recognizable field before emitting
      filter(info => !!info.version || !!info.uptimeSeconds),
      shareReplay(1)
    );
  }

  private connect(): Observable<any> {
    if (environment.mock || this.socket$ || !window.location.host) {
      return EMPTY;
    }

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const host = window.location.host;
    const url = `${protocol}//${host}/api/ws/live`;

    this.socket$ = webSocket({
      url,
      openObserver: {
        next: () => {
          console.log('Live WebSocket connected');
          this.connectedSubject.next(true);
        }
      },
      closeObserver: {
        next: () => {
          console.log('Live WebSocket disconnected');
          this.connectedSubject.next(false);
          this.socket$ = null;
        }
      }
    });

    return this.socket$.pipe(
      timeout(5000),
      tap(msg => {
        if (msg.event === 'update' && msg.data) {
          this.updates$.next(msg.data);
        }
      }),
      retry({ delay: 5000 }),
      share()
    );
  }
}
