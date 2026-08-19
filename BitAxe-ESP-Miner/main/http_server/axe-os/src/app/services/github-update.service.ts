import { HttpClient } from '@angular/common/http';
import { Injectable } from '@angular/core';
import { Observable } from 'rxjs';
import { map } from 'rxjs/operators';


interface GithubRelease {
  id: number;
  tag_name: string;
  name: string;
  prerelease: boolean;
}

@Injectable({
  providedIn: 'root'
})
export class GithubUpdateService {

  constructor(
    private httpClient: HttpClient
  ) { }


  public getReleases(): Observable<GithubRelease[]> {
    // SerpentX dual-pool fork: show OUR releases in the update panel, not upstream
    // bitaxeorg/esp-miner (whose firmware would overwrite the dual-pool build).
    return this.httpClient.get<GithubRelease[]>(
      'https://api.github.com/repos/SerpentXSF/Dual-Pool-Mining-NerdAxe-BitAxe/releases'
    ).pipe(
      map((releases: GithubRelease[]) => releases.filter((release: GithubRelease) => !release.prerelease))
    );
  }

}
