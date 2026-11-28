import { Link } from "@tanstack/react-router";
import { Coins, User, ArrowLeft, LogOut, Copy } from "lucide-react";
import { toast } from "sonner";
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
  AlertDialogTrigger,
} from "@/components/ui/alert-dialog";
import { Button } from "@/components/ui/button";
import { MAX_LEVEL } from "@/lib/scoring";

interface ShopHeaderProps {
  userName: string;
  buddyName: string;
  coinBalance: number;
  level: number;
  avatarUrl: string | null;
  activePage: "shop" | "profile";
  deviceToken?: string | null;
  onLogout: () => void;
}

export function ShopHeader({
  userName,
  buddyName,
  coinBalance,
  level,
  avatarUrl,
  activePage,
  deviceToken = null,
  onLogout,
}: ShopHeaderProps) {
  const copyToken = async () => {
    if (!deviceToken) return;
    try {
      await navigator.clipboard.writeText(deviceToken);
      toast.success("Device token copied.");
    } catch {
      toast.error("Could not copy. Open Profile to select the token manually.");
    }
  };

  return (
    <header className="sticky top-0 z-30 border-b border-border/80 bg-background/85 backdrop-blur-md">
      <div className="mx-auto flex max-w-6xl items-center justify-between gap-3 px-4 py-3 sm:px-6">
        <div className="flex items-center gap-3">
          <Link to="/" className="flex items-center gap-2.5">
            <span className="flex size-8 items-center justify-center rounded-lg bg-primary text-sm font-bold text-primary-foreground">
              C
            </span>
            <div className="leading-tight">
              <div className="text-sm font-semibold tracking-tight text-foreground">codo</div>
              <div className="font-mono text-[11px] text-muted-foreground">{buddyName || "desk buddy"}</div>
            </div>
          </Link>
        </div>

        <div className="flex items-center gap-2 sm:gap-3">
          <div className="flex items-center gap-1.5 rounded-full border border-coin/35 bg-card px-3 py-1.5 font-mono text-xs font-medium text-coin">
            <Coins className="size-3.5" />
            <span>{coinBalance.toLocaleString()}</span>
            <span className="hidden text-muted-foreground sm:inline">codocoins</span>
          </div>

          <div className="hidden items-center gap-1.5 rounded-full border border-lavender/35 bg-card px-3 py-1.5 font-mono text-xs text-lavender sm:flex">
            <span>Lv {level}</span>
            <span className="text-muted-foreground">/ {MAX_LEVEL}</span>
          </div>

          {activePage === "shop" ? (
            <Link
              to="/profile"
              aria-label="Open profile"
              className="flex items-center gap-2 rounded-full border border-border bg-card py-1 pr-3 pl-1 text-xs font-medium text-foreground transition-colors hover:border-primary/70"
            >
              {avatarUrl ? (
                <img src={avatarUrl} alt={userName || "Profile"} className="size-7 rounded-full object-cover" />
              ) : (
                <span className="flex size-7 items-center justify-center rounded-full bg-secondary text-foreground">
                  <User className="size-3.5" />
                </span>
              )}
              <span className="max-w-24 truncate">{userName || "Profile"}</span>
            </Link>
          ) : (
            <Link
              to="/"
              className="flex items-center gap-1.5 rounded-full border border-border bg-card px-3.5 py-1.5 text-xs font-medium text-foreground transition-colors hover:border-primary/70"
            >
              <ArrowLeft className="size-3.5" />
              Shop
            </Link>
          )}

          <AlertDialog>
            <AlertDialogTrigger asChild>
              <button
                type="button"
                aria-label="Log out"
                className="flex items-center gap-1.5 rounded-full border border-border bg-card px-3 py-1.5 text-xs font-medium text-muted-foreground transition-colors hover:border-destructive/70 hover:text-destructive"
              >
                <LogOut className="size-3.5" />
                <span className="hidden sm:inline">Log out</span>
              </button>
            </AlertDialogTrigger>
            <AlertDialogContent>
              <AlertDialogHeader>
                <AlertDialogTitle>Log out of Codo?</AlertDialogTitle>
                <AlertDialogDescription>
                  This only signs you out on this browser. {buddyName || "Your desk buddy"}, your codocoins and
                  accessories stay saved, and your robot keeps working. To get back in you will need your
                  device token, so copy it before you go.
                </AlertDialogDescription>
              </AlertDialogHeader>
              {deviceToken && (
                <Button type="button" variant="outline" onClick={copyToken} className="w-full">
                  <Copy className="mr-2 size-3.5" />
                  Copy device token
                </Button>
              )}
              <AlertDialogFooter>
                <AlertDialogCancel>Stay logged in</AlertDialogCancel>
                <AlertDialogAction onClick={onLogout}>Log out</AlertDialogAction>
              </AlertDialogFooter>
            </AlertDialogContent>
          </AlertDialog>
        </div>
      </div>
    </header>
  );
}