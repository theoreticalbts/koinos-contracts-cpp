#include <koinos/system/system_calls.hpp>
#include <koinos/contracts/token/token.h>

#include <koinos/buffer.hpp>
#include <koinos/common.h>

#include <boost/multiprecision/cpp_int.hpp>

#include <string>

using namespace koinos;
using namespace koinos::contracts;

using int128_t = boost::multiprecision::int128_t;

namespace constants {

static const std::string koinos_name   = "Test Koinos";
static const std::string koinos_symbol = "tKOIN";
constexpr uint32_t koinos_decimals     = 8;
constexpr uint64_t mana_regen_time     = 432'000; // 5 days
constexpr std::size_t max_address_size = 25;
constexpr std::size_t max_name_size    = 32;
constexpr std::size_t max_symbol_size  = 8;
constexpr std::size_t max_buffer_size  = 2048;
std::string supply_key                 = "";
std::string contract_space             = system::get_contract_id();

} // constants

enum entries : uint32_t
{
   name_entry         = 0x76ea4297,
   symbol_entry       = 0x7e794b24,
   decimals_entry     = 0x59dc15ce,
   total_supply_entry = 0xcf2e8212,
   balance_of_entry   = 0x15619248,
   transfer_entry     = 0x62efa292,
   mint_entry         = 0xc2f82bdc
};

token::name_result< constants::max_name_size > name()
{
   token::name_result< constants::max_name_size > res;
   res.mutable_value() = constants::koinos_name.c_str();
   return res;
}

token::symbol_result< constants::max_symbol_size > symbol()
{
   token::symbol_result< constants::max_symbol_size > res;
   res.mutable_value() = constants::koinos_symbol.c_str();
   return res;
}

token::decimals_result decimals()
{
   token::decimals_result res;
   res.mutable_value() = constants::koinos_decimals;
   return res;
}

token::total_supply_result total_supply()
{
   token::total_supply_result res;

   token::balance_object bal_obj;
   system::get_object( constants::contract_space, constants::supply_key, bal_obj );

   res.mutable_value() = bal_obj.get_value();
   return res;
}

token::balance_of_result balance_of( const token::balance_of_arguments< constants::max_address_size >& args )
{
   token::balance_of_result res;

   std::string owner( reinterpret_cast< const char* >( args.get_owner().get_const() ), args.get_owner().get_length() );

   token::mana_balance_object bal_obj;
   system::get_object( constants::contract_space, owner, bal_obj );

   res.set_value( bal_obj.get_balance() );
   return res;
}

void regenerate_mana( token::mana_balance_object& bal )
{
   auto head_block_time = system::get_head_info().head_block_time();
   auto delta = std::min( head_block_time - bal.last_mana_update(), constants::mana_regen_time );
   if ( delta )
   {
      auto new_mana = bal.mana() + ( ( int128_t( delta ) * int128_t( bal.balance() ) ) / constants::mana_regen_time ).convert_to< uint64_t >() ;
      bal.set_mana( std::min( new_mana, bal.mana() ) );
      bal.set_last_mana_update( head_block_time );
   }
}

token::transfer_result transfer( const token::transfer_arguments< constants::max_address_size, constants::max_address_size >& args )
{
   token::transfer_result res;
   res.set_value( false );

   std::string from( reinterpret_cast< const char* >( args.get_from().get_const() ), args.get_from().get_length() );
   std::string to( reinterpret_cast< const char* >( args.get_to().get_const() ), args.get_to().get_length() );
   uint64_t value = args.get_value();

   system::require_authority( from );

   token::mana_balance_object from_bal_obj;
   if ( !system::get_object( constants::contract_space, from, from_bal_obj ) )
   {
      system::print( "could not read 'from' balance" );
      return res;
   }

   if ( from_bal_obj.balance() < value )
   {
      system::print( "'from' has insufficient balance" );
      return res;
   }

   regenerate_mana( from_bal_obj );

   if ( from_bal_obj.mana() < value )
   {
      system::print( "'from' has insufficient mana for transfer" );
      return res;
   }

   token::mana_balance_object to_bal_obj;
   system::get_object( constants::contract_space, to, to_bal_obj );

   regenerate_mana( to_bal_obj );

   from_bal_obj.set_balance( from_bal_obj.balance() - value );
   from_bal_obj.set_mana( from_bal_obj.mana() - value );
   to_bal_obj.set_balance( to_bal_obj.balance() + value );
   to_bal_obj.set_mana( to_bal_obj.mana() + value );

   if ( !system::put_object( constants::contract_space, from, from_bal_obj ) )
   {
      system::print( "could not write 'from' balance" );
      return res;
   }

   if ( !system::put_object( constants::contract_space, to, to_bal_obj ) )
   {
      system::print( "could not write 'to' balance" );
      return res;
   }

   res.set_value( true );
   return res;
}

token::mint_result mint( const token::mint_arguments< constants::max_address_size >& args )
{
   token::mint_result res;
   res.set_value( false );

   std::string to( reinterpret_cast< const char* >( args.get_to().get_const() ), args.get_to().get_length() );
   uint64_t amount = args.get_value();

   const auto [ caller, privilege ] = system::get_caller();
   if ( privilege != chain::privilege::kernel_mode )
   {
      system::print( "can only mint token from kernel context" );
      return res;
   }

   auto supply = total_supply().get_value();
   auto new_supply = supply + amount;

   // Check overflow
   if ( new_supply < supply )
   {
      system::print( "mint would overflow supply" );
      return res;
   }

   token::mana_balance_object to_bal_obj;
   system::get_object( constants::contract_space, to, to_bal_obj );

   regenerate_mana( to_bal_obj );

   to_bal_obj.set_balance( to_bal_obj.balance() + amount );
   to_bal_obj.set_mana( to_bal_obj.mana() + amount );

   token::balance_object supply_obj;
   supply_obj.set_value( new_supply );

   if( !system::put_object( constants::contract_space, constants::supply_key, supply_obj ) )
   {
      system::print( "could not write token supply" );
      return res;
   }

   if( !system::put_object( constants::contract_space, to, to_bal_obj ) )
   {
      system::print( "could not write 'to' balance" );
      return res;
   }

   res.set_value( true );
   return res;
}

int main()
{
   auto entry_point = system::get_entry_point();
   auto args = system::get_contract_arguments();

   std::array< uint8_t, constants::max_buffer_size > retbuf;

   koinos::read_buffer rdbuf( (uint8_t*)args.c_str(), args.size() );
   koinos::write_buffer buffer( retbuf.data(), retbuf.size() );

   switch( std::underlying_type_t< entries >( entry_point ) )
   {
      case entries::name_entry:
      {
         auto res = name();
         res.serialize( buffer );
         break;
      }
      case entries::symbol_entry:
      {
         auto res = symbol();
         res.serialize( buffer );
         break;
      }
      case entries::decimals_entry:
      {
         auto res = decimals();
         res.serialize( buffer );
         break;
      }
      case entries::total_supply_entry:
      {
         auto res = total_supply();
         res.serialize( buffer );
         break;
      }
      case entries::balance_of_entry:
      {
         token::balance_of_arguments< constants::max_address_size > arg;
         arg.deserialize( rdbuf );

         auto res = balance_of( arg );
         res.serialize( buffer );
         break;
      }
      case entries::transfer_entry:
      {
         token::transfer_arguments< constants::max_address_size, constants::max_address_size > arg;
         arg.deserialize( rdbuf );

         auto res = transfer( arg );
         res.serialize( buffer );
         break;
      }
      case entries::mint_entry:
      {
         token::mint_arguments< constants::max_address_size > arg;
         arg.deserialize( rdbuf );

         auto res = mint( arg );
         res.serialize( buffer );
         break;
      }
      default:
         system::exit_contract( 1 );
   }

   std::string retval( reinterpret_cast< const char* >( buffer.data() ), buffer.get_size() );
   system::set_contract_result_bytes( retval );

   system::exit_contract( 0 );
   return 0;
}
