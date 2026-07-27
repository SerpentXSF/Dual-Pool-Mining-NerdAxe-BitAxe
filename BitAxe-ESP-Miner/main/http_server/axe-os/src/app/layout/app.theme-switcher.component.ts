import { Component, OnInit } from '@angular/core';

interface Accent {
  name: string;
  color: string; // --primary-color
  text: string;  // --primary-color-text (readable contrast)
}

/**
 * SerpentX accent switcher (isolated, browser-saved).
 *
 * The whole AxeOS theme derives from a single CSS variable, `--primary-color`
 * (see layout/styles/layout/_theme.scss). Overriding it (plus its contrast
 * `--primary-color-text`) on the document root re-accents the entire UI at
 * runtime. The choice persists in localStorage only — no firmware/NVS change,
 * so this is a pure front-end feature kept in one self-contained component to
 * minimise the upstream-merge surface.
 */
@Component({
  selector: 'app-theme-switcher',
  standalone: false,
  template: `
    <a class="block py-2 text-color cursor-pointer"
       tooltipPosition="bottom" pTooltip="Accent colour"
       (click)="panel.toggle($event)">
      <i class="pi pi-palette text-xl block"></i>
    </a>
    <p-popover #panel>
      <div class="flex flex-column gap-2" style="min-width: 12rem;">
        <span class="text-sm font-medium">Accent colour</span>
        <div class="flex flex-wrap gap-2">
          <button *ngFor="let a of accents" type="button"
                  class="cursor-pointer border-round border-none p-0"
                  style="width: 1.7rem; height: 1.7rem;"
                  [style.background]="a.color"
                  [style.box-shadow]="a.color.toLowerCase() === current.toLowerCase()
                    ? '0 0 0 2px var(--surface-card), 0 0 0 4px var(--text-color)' : 'none'"
                  [pTooltip]="a.name" tooltipPosition="top"
                  (click)="apply(a)"></button>
        </div>
        <label class="flex align-items-center justify-content-between gap-2 mt-1 text-sm cursor-pointer">
          <span>Custom</span>
          <input type="color" [value]="current"
                 (input)="applyCustom($any($event.target).value)"
                 style="width: 2.2rem; height: 1.7rem; padding: 0; border: none; background: none; cursor: pointer;" />
        </label>
      </div>
    </p-popover>
  `,
})
export class ThemeSwitcherComponent implements OnInit {
  private static readonly KEY = 'serpentx-accent';

  // Default is AxeOS Blue (first entry). The rest, incl. SerpentX Gold, are opt-in.
  public accents: Accent[] = [
    { name: 'AxeOS Blue',    color: '#3B82F6', text: '#ffffff' },
    { name: 'SerpentX Gold', color: '#F7B32B', text: '#1a1a1a' },
    { name: 'AxeOS Red',     color: '#F80421', text: '#ffffff' },
    { name: 'Emerald',       color: '#10B981', text: '#04241a' },
    { name: 'Violet',        color: '#8B5CF6', text: '#ffffff' },
    { name: 'Cyan',          color: '#06B6D4', text: '#04242b' },
  ];

  public current: string = this.accents[0].color;

  ngOnInit(): void {
    const saved = this.read();
    this.set(saved?.color ?? this.accents[0].color,
             saved?.text ?? this.accents[0].text,
             false);
  }

  public apply(a: Accent): void {
    this.set(a.color, a.text, true);
  }

  public applyCustom(color: string): void {
    this.set(color, this.isLight(color) ? '#1a1a1a' : '#ffffff', true);
  }

  private set(color: string, text: string, persist: boolean): void {
    this.current = color;
    const root = document.documentElement;
    root.style.setProperty('--primary-color', color);
    root.style.setProperty('--primary-color-text', text);
    if (persist) {
      try {
        localStorage.setItem(ThemeSwitcherComponent.KEY, JSON.stringify({ color, text }));
      } catch { /* private mode / quota — ignore, in-memory still applied */ }
    }
  }

  private read(): { color: string; text: string } | null {
    try {
      const s = localStorage.getItem(ThemeSwitcherComponent.KEY);
      return s ? JSON.parse(s) : null;
    } catch {
      return null;
    }
  }

  private isLight(hex: string): boolean {
    const c = hex.replace('#', '');
    if (c.length !== 6) return false;
    const r = parseInt(c.substring(0, 2), 16);
    const g = parseInt(c.substring(2, 4), 16);
    const b = parseInt(c.substring(4, 6), 16);
    return (0.299 * r + 0.587 * g + 0.114 * b) > 150;
  }
}
