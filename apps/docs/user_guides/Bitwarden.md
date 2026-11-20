# BitWarden

A **read-only** copy of your Bitwarden or Vaultwarden vault on the Palm. The Palm does no network work: a **WiRSa** logs in, downloads and decrypts the vault, and sends it over the serial cable one item at a time. The Palm keeps the items encrypted with your Sec35 password ([Getting started](Getting_started.md)).

Needs: a WiRSa on WiFi ([WiRSa](WiRSa.md)), and the Sec35 password set. Tick **Screenless Mode** in Preferences and the app starts the WiRSa's BitWarden mode for you; otherwise choose **BitWarden Relay** on the WiRSa first (or in [WiRSaControl](WiRSaControl.md)).

![List](img/bw_main.png)

## First sync
1. **Options > Preferences.** Enter the **Server** (the address of your Vaultwarden or Bitwarden server) and your **Email**.

   ![Preferences](img/bw_prefs.png)

2. Tap **Sync Now** and type your **master password**. It goes to the WiRSa for this sync only.

   ![Unlock](img/bw_unlock.png)

3. The first time, the app shows the server's certificate fingerprint. Check it, then tap **Trust**. It is remembered per server (**Forget Trust** in Preferences clears it).

   ![Trust](img/bw_trust.png)

4. If the account has two-step login, a keypad asks for the code. Tap **Cancel** on the progress screen to stop a sync at any time.

A short tune plays when a sync starts and ends. A new sync replaces the old copy only if it completes, so a failed one never loses your data.

## Finding things
- The pop-up at the top right lists your **folders**, then (below a line) your **organizations**.

  ![Folders](img/bw_folders.png)

- The pop-up at the bottom left filters by type: **Login**, **Note**, **Card** or **Identity**. The icon in each row shows the type.

  ![Types](img/bw_types.png)

- Type in **Find** at the bottom to narrow the list; **Clear** empties it.

## Reading an item
Tap an item. Passwords are hidden; **Show** reveals them (and the button then says **Hide**). Other fields follow the item type (card number and code, identity details, notes).

![Item](img/bw_item.png)

![Shown](img/bw_item_shown.png)

## Archived items
Items you archived in Bitwarden are **not mixed into the list**. Open **Options > Archived Items** to see only those; the menu entry then reads **Active Items**, which takes you back.

![Archived](img/bw_archived.png)

![Menu](img/bw_menu2.png)

## Locking
The vault is hidden (`******`) when Sec35 is locked, after the **Lock after** time in Preferences, or when you choose **Lock Now**. **Delete Account** removes the local copy. **Security > 2FA** chooses how the two-step code is entered.
