# Corrections 2

## file types
- There should be a difference betweena a single media item and a song or sermon presentation.
- Add next to Content Library and Service Playlist a Media Library. These will content single media files.

## Lyrics
- Lyrics should be able to be adjusted by groupings such as verse Chorus, bridge etc
- A user should be able to make a song arraingment based on these groups where it allows a user during a presentation to simply click through the lyrics and not have to go back manually.
- In editing if the chorus is changed it should make the adjustment for all instances of that songs chorus. The same for all parts of a song.

## Long Term Storage
- There should be an option to clean up things unused over a user specified amount of time.
- the use could enter a certain number of days to filter things down to and then delete things unused over a specified time period.

## Slide formatting
- A user should be able to set a "look" or a "scene" for a slide that is then applicable to all the slides in a presentation.
- For example they set a certain font and font size for a slide. They should be able to apply that to all the slides in a presentation.
- There should be error handling when this is applied if the users specifications make the text too big for the slide there should be an option to auto split slides that have too much text to fit.

## navigation
- A user should be able to progress through the slides of a servive with the space bar or with arrows.
- Currently it only allows for space bar and there is no way to go backwards.

## App Shipping
- The app should require an authorization code to function for presentations.
- Without the code it should function as an editing platform for services.
- Create an Authorization code that is not committed to github for me to use in development
- I want the authorization code to be created on cloudflare and stored in a cloudflare DB
- Somehow the app should self-check for a valid auth code at startup and not be tamperable by the user.
- If the code isn't valid on the local machine it should check with the cloudflare DB
- If that isn't valid the user should be prompted to get a valid auth code.
- I also want you to package the app for shipping and have a one click install.
    - There should be a file that I can post on my website a user can download and then with one click install on their machine.

## MCP
- I want the MCP to be a key component of this app and I want the user to have clear instructions on how to use it.

## Help
- I still don't see a local documentation hub for this app.
- A user should have access to local documentation for this app.