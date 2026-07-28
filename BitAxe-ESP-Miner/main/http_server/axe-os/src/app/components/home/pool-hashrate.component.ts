import { Component, Input } from '@angular/core';

/**
 * Per-pool hashrate card shown below the dashboard grid in dual mode — extracted
 * from home.component so the upstream home template carries only a one-line
 * <app-pool-hashrate [info]="info"/>. Purely presentational; `info` is the live
 * system-info object (custom dual fields accessed via $any in the template).
 */
@Component({
  selector: 'app-pool-hashrate',
  standalone: false,
  template: `
    <div class="card mb-0 mt-3" *ngIf="$any(info).dualEnable">
        <span class="block text-lg font-medium mb-3">Per-Pool Hashrate &mdash; Dual Mode</span>
        <div class="grid">
            <div class="col-12 md:col-6">
                <div class="p-3 border-round" style="background: rgba(124,141,255,0.10);">
                    <div class="flex justify-content-between align-items-center">
                        <span class="text-lg font-medium">Pool A</span>
                        <span class="text-500 text-sm">{{ $any(info).dualRatioA }}% share</span>
                    </div>
                    <div class="text-3xl font-bold my-2">{{ (info.hashRate * $any(info).dualRatioA / 100) | hashSuffix }}</div>
                    <div class="text-500 text-sm break-all">{{ info.stratumURL }}<span *ngIf="$any(info).isUsingFallbackStratum"> (fallback)</span></div>
                    <div class="text-500 text-sm">Accepted shares: {{ $any(info).poolASharesAccepted }}</div>
                </div>
            </div>
            <div class="col-12 md:col-6">
                <div class="p-3 border-round" style="background: rgba(45,212,191,0.10);">
                    <div class="flex justify-content-between align-items-center">
                        <span class="text-lg font-medium">Pool B</span>
                        <span class="text-500 text-sm">{{ 100 - $any(info).dualRatioA }}% share</span>
                    </div>
                    <div class="text-3xl font-bold my-2">{{ (info.hashRate * (100 - $any(info).dualRatioA) / 100) | hashSuffix }}</div>
                    <div class="text-500 text-sm break-all">{{ $any(info).poolBUrl }}</div>
                    <div class="text-500 text-sm">Accepted shares: {{ $any(info).poolBSharesAccepted }}</div>
                </div>
            </div>
        </div>
        <div class="text-500 text-xs mt-2">Per-pool rate = total hashrate &times; each pool's time-slice share (one ASIC, time-shared).</div>
    </div>
  `,
})
export class PoolHashrateComponent {
  @Input() info: any;
}
