import { Component } from '@angular/core';
import { ControlContainer, FormGroupDirective } from '@angular/forms';

/**
 * Dual mining (Pool B) settings block — extracted from pool.component so the
 * upstream pool template carries only a one-line <app-dual-mining-settings/>.
 *
 * The viewProviders line re-exposes the parent form's ControlContainer, so the
 * formControlName bindings below resolve against the parent pool FormGroup with
 * no @Input plumbing — the controls themselves stay defined in pool.component.ts.
 * This keeps our whole dual-mining UI delta in one file that never collides with
 * upstream's pool.component.html.
 */
@Component({
  selector: 'app-dual-mining-settings',
  standalone: false,
  viewProviders: [{ provide: ControlContainer, useExisting: FormGroupDirective }],
  template: `
    <h2 class="mt-5">Dual Mining (Simultaneous Pool B)</h2>
    <div class="card">
        <div class="field-checkbox grid">
            <div class="col-1 md:col-10 md:flex-order-2">
                <p-checkbox name="dualEnable" inputId="dualEnable" formControlName="dualEnable" [binary]="true"></p-checkbox>
            </div>
            <label htmlFor="dualEnable" class="col-11 m-0 pb-0 pl-3 md:col-2 md:flex-order-1 md:pl-2">
                <tooltip-text-icon
                    text="Enable Dual Mining"
                    tooltip="Keep two Stratum connections live at once and time-slice the ASIC between them. When off, behaviour is identical to single-pool. Both pools must be SHA-256d (Bitcoin-header) pools; total hashrate is split, not doubled."
                />
            </label>
        </div>

        <div class="field grid p-fluid">
            <label htmlFor="dualIntervalMs" class="col-12 md:col-2 md:mb-0">
                <tooltip-text-icon text="Split Interval (ms)"
                    tooltip="Length of each time slice. The pool that owns a slice feeds the ASIC for that interval. Default 3000 ms (range 100–60000). Lower values switch more often and raise the ASIC error rate — 500 ms is too aggressive." />
            </label>
            <div class="col-12 md:col-10">
                <input pInputText id="dualIntervalMs" formControlName="dualIntervalMs" type="number" />
            </div>
        </div>

        <div class="field grid p-fluid">
            <label htmlFor="dualRatioA" class="col-12 md:col-2 md:mb-0">
                <tooltip-text-icon text="Pool A Share %"
                    tooltip="Percentage of hashing time given to Pool A (the primary pool above). Pool B gets the remainder. 70 = 70/30, 50 = 50/50, 25 = 25/75, etc." />
            </label>
            <div class="col-12 md:col-10">
                <input pInputText id="dualRatioA" formControlName="dualRatioA" type="number" min="0" max="100" />
            </div>
        </div>

        <h3 class="mt-4">Pool B</h3>
        <div class="field grid p-fluid">
            <label htmlFor="poolBUrl" class="col-12 md:col-2 md:mb-0">Stratum Host</label>
            <div class="col-12 md:col-10">
                <input pInputText id="poolBUrl" formControlName="poolBUrl" type="text" />
            </div>
        </div>
        <div class="field grid p-fluid">
            <label htmlFor="poolBPort" class="col-12 md:col-2 md:mb-0">Stratum Port</label>
            <div class="col-12 md:col-10">
                <input pInputText id="poolBPort" formControlName="poolBPort" type="number" />
            </div>
        </div>
        <div class="field grid p-fluid">
            <label htmlFor="poolBUser" class="col-12 md:col-2 md:mb-0">User</label>
            <div class="col-12 md:col-10">
                <input pInputText id="poolBUser" formControlName="poolBUser" sensitive-data type="text" />
            </div>
        </div>
        <div class="field grid p-fluid">
            <label htmlFor="poolBPassword" class="col-12 md:col-2 md:mb-0">Pool Password</label>
            <div class="col-12 md:col-10">
                <input pInputText id="poolBPassword" formControlName="poolBPassword" type="password" autocomplete="off" />
            </div>
        </div>
        <div class="field grid p-fluid">
            <label htmlFor="poolBTLS" class="col-12 md:col-2 md:mb-0">
                <tooltip-text-icon text="TLS"
                    tooltip="0 = disabled, 1 = bundled CA certificate, 2 = custom certificate." />
            </label>
            <div class="col-12 md:col-10">
                <input pInputText id="poolBTLS" formControlName="poolBTLS" type="number" min="0" max="2" />
            </div>
        </div>

        <h3 class="mt-4">Pool B Failover</h3>
        <div class="field grid p-fluid">
            <label htmlFor="poolBFallbackUrl" class="col-12 md:col-2 md:mb-0">Stratum Host</label>
            <div class="col-12 md:col-10">
                <input pInputText id="poolBFallbackUrl" formControlName="poolBFallbackUrl" type="text" />
            </div>
        </div>
        <div class="field grid p-fluid">
            <label htmlFor="poolBFallbackPort" class="col-12 md:col-2 md:mb-0">Stratum Port</label>
            <div class="col-12 md:col-10">
                <input pInputText id="poolBFallbackPort" formControlName="poolBFallbackPort" type="number" />
            </div>
        </div>
        <div class="field grid p-fluid">
            <label htmlFor="poolBFallbackUser" class="col-12 md:col-2 md:mb-0">User</label>
            <div class="col-12 md:col-10">
                <input pInputText id="poolBFallbackUser" formControlName="poolBFallbackUser" sensitive-data type="text" />
            </div>
        </div>
        <div class="field grid p-fluid">
            <label htmlFor="poolBFallbackPassword" class="col-12 md:col-2 md:mb-0">Pool Password</label>
            <div class="col-12 md:col-10">
                <input pInputText id="poolBFallbackPassword" formControlName="poolBFallbackPassword" type="password" autocomplete="off" />
            </div>
        </div>
        <div class="field grid p-fluid mb-0">
            <label htmlFor="poolBFallbackTLS" class="col-12 md:col-2 md:mb-0">
                <tooltip-text-icon text="TLS"
                    tooltip="0 = disabled, 1 = bundled CA certificate, 2 = custom certificate." />
            </label>
            <div class="col-12 md:col-10">
                <input pInputText id="poolBFallbackTLS" formControlName="poolBFallbackTLS" type="number" min="0" max="2" />
            </div>
        </div>
    </div>
  `,
})
export class DualMiningSettingsComponent {}
